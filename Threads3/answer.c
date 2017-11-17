#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_cond_reader = PTHREAD_COND_INITIALIZER;
pthread_cond_t g_cond_writer = PTHREAD_COND_INITIALIZER;

#define G_BUFFER_EMPTY (-1)
int g_bufferSizeInBytes = G_BUFFER_EMPTY;
#define G_BUFFER_MAXLEN (100)
char g_buffer[G_BUFFER_MAXLEN];

int get_external_data(char *buffer, int bufferSizeInBytes) {
	int wait = rand() / (RAND_MAX / 10);

	usleep(wait);
	return bufferSizeInBytes;
}

void process_data(char *buffer, int bufferSizeInBytes) {
	int wait = rand() / (RAND_MAX / 20);

	usleep(wait);
}

/**
 * This thread is responsible for pulling data off of the shared data
 * area and processing it using the process_data() API.
 */
void *reader_thread(void *arg) {
	int num = (int) arg;
	char buff[G_BUFFER_MAXLEN];
	int len;

	printf("reader_thread(%d) start.\n", num);

	while(1) {
		pthread_mutex_lock(&g_mutex);
		while (g_bufferSizeInBytes == G_BUFFER_EMPTY) {
			pthread_cond_wait(&g_cond_reader, &g_mutex);
		}
		len = g_bufferSizeInBytes;
		memcpy(buff, g_buffer, len);
		g_bufferSizeInBytes = G_BUFFER_EMPTY;
		pthread_cond_signal(&g_cond_writer);
		pthread_mutex_unlock(&g_mutex);

		printf("reader_thread(%d) took data (len=%d).\n", num, len);
		process_data(buff, len);
		printf("reader_thread(%d) processed data (len=%d).\n", num, len);
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
	char buff[WRITER_BUFF_SIZE];
	int len;

	assert(WRITER_BUFF_SIZE < G_BUFFER_MAXLEN);
	printf("writer_thread(%d) start.\n", num);

	while(1) {
		len = get_external_data(buff, WRITER_BUFF_SIZE);
		if (len < 0) {
			printf("*** writer_thread(%d): get_external_data error!\n");
			continue;
		}
		printf("writer_thread(%d) got data (len=%d).\n", num, len);

		pthread_mutex_lock(&g_mutex);
		while (g_bufferSizeInBytes != G_BUFFER_EMPTY) {
			pthread_cond_wait(&g_cond_writer, &g_mutex);
		}
		g_bufferSizeInBytes = len;
		memcpy(g_buffer, buff, len);
		pthread_cond_signal(&g_cond_reader);
		pthread_mutex_unlock(&g_mutex);

		printf("writer_thread(%d) wrote data (len=%d).\n", num, len);
	}

	return NULL;
}


#define M 10
#define N 20
int main(int argc, char **argv) {
	int i;
	pthread_t reader_threads[N], writer_threads[M];

	for(i = 0; i < N; i++) {
		pthread_create(&reader_threads[i], NULL, reader_thread, (void *) i);
	}

	for(i = 0; i < M; i++) {
		pthread_create(&writer_threads[i], NULL, writer_thread, (void *) i);
	}

	pthread_join(reader_threads[0], NULL); // will block
	return 0;
}
