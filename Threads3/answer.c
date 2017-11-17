#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include "queue.h"

#define M 10
#define N 20

pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_cond_reader = PTHREAD_COND_INITIALIZER;
pthread_cond_t g_cond_writer = PTHREAD_COND_INITIALIZER;

#define G_BUFFER_MAXLEN (100)
struct g_buffer {
	TAILQ_ENTRY(g_buffer) gb_entry;
	int len;
	char buff[G_BUFFER_MAXLEN];
};

TAILQ_HEAD(, g_buffer) g_buffer_queue;
#define G_BUFFER_QUEUE_MAXLEN (16)
int g_buffer_queue_len = 0;

int g_num_reader_threads = 0;

struct g_buffer *get_external_data(int len) {
	struct g_buffer *buff_p;
	int wait = rand() / (RAND_MAX / 10);

	// Wait to get external data
	usleep(wait);

	// Produce buffer
	buff_p = malloc(sizeof(struct g_buffer));
	assert(buff_p != NULL);
	buff_p->len = len;
	return buff_p;
}

void process_data(struct g_buffer *buff_p) {
	int wait = rand() / (RAND_MAX / 10);

	assert(buff_p != NULL);

	// Wait to process data
	usleep(wait);

	// Consume buffer
	free(buff_p);
}

/**
 * This thread is responsible for pulling data off of the shared data
 * area and processing it using the process_data() API.
 */
void *reader_thread(void *arg) {
	int num = (int) arg;
	struct g_buffer *buff_p;

	printf("reader_thread(%d) start.\n", num);

	while(1) {
		pthread_mutex_lock(&g_mutex);
		if (g_num_reader_threads < N) {
			g_num_reader_threads++;
		}

		while ((buff_p = TAILQ_FIRST(&g_buffer_queue)) == NULL) {
			// printf("reader_thread(%d) wait until getting more queue.\n", num);
			pthread_cond_signal(&g_cond_writer);
			pthread_cond_wait(&g_cond_reader, &g_mutex);
		}
		g_buffer_queue_len--;
		TAILQ_REMOVE(&g_buffer_queue, buff_p, gb_entry);
		printf("reader_thread(%d) size of queue = %d.\n",
		    num, g_buffer_queue_len);
		pthread_cond_signal(&g_cond_writer);
		pthread_mutex_unlock(&g_mutex);

		printf("reader_thread(%d) took data (len=%d).\n",
		    num, buff_p->len);
		process_data(buff_p);
		printf("reader_thread(%d) processed data.\n", num);
	}

	return NULL;
}


/**
 * This thread is responsible for pulling data from a device using
 * the get_external_data() API and placing it into a shared area
 * for later processing by one of the reader threads.
 */
void *writer_thread(void *arg) {
	int num = (int) arg;
#define WRITER_BUFF_SIZE (num * 10)
	struct g_buffer *buff_p;

	assert(WRITER_BUFF_SIZE < G_BUFFER_MAXLEN);
	printf("writer_thread(%d) start.\n", num);

	while(1) {
		buff_p = get_external_data(WRITER_BUFF_SIZE);
		printf("writer_thread(%d) got data (len=%d).\n",
		    num, buff_p->len);

		pthread_mutex_lock(&g_mutex);
		while (g_buffer_queue_len >= G_BUFFER_QUEUE_MAXLEN) {
			// printf("writer_thread(%d) wait until getting less queue\n", num);
			pthread_cond_signal(&g_cond_reader);
			pthread_cond_wait(&g_cond_writer, &g_mutex);
		}
		g_buffer_queue_len++;
		TAILQ_INSERT_TAIL(&g_buffer_queue, buff_p, gb_entry);
		printf("writer_thread(%d) size of queue = %d.\n",
		    num, g_buffer_queue_len);
		pthread_cond_signal(&g_cond_reader);
		pthread_mutex_unlock(&g_mutex);

		printf("writer_thread(%d) wrote data.\n", num);
	}

	return NULL;
}

void wait_ready_reader_threads(int num_thread) {
	pthread_mutex_lock(&g_mutex);
	while (g_num_reader_threads < num_thread) {
		printf("g_num_reader_threads = %d\n", g_num_reader_threads);
		pthread_cond_wait(&g_cond_writer, &g_mutex);
	}
	pthread_mutex_unlock(&g_mutex);
}

int main(int argc, char **argv) {
	int i;
	pthread_t reader_threads[N], writer_threads[M];

	TAILQ_INIT(&g_buffer_queue);

	for(i = 0; i < N; i++) {
		pthread_create(&reader_threads[i], NULL, reader_thread, (void *) i);
	}
	wait_ready_reader_threads(N);
	for(i = 0; i < M; i++) {
		pthread_create(&writer_threads[i], NULL, writer_thread, (void *) i);
	}

	pthread_join(reader_threads[0], NULL); // will block
	return 0;
}
