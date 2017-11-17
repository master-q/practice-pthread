#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <string.h>


/*** DEFINES ***/
#define true 1
#define false 0
#define BUFFSZ 4096

#define PROCESS_CNT 100000
#define MAXBUFFS 2 

#define M 10
#define N 20


/*** STRUCTS ***/
typedef struct __attribute__((__packed__)) Msg_t{
	char buffer[BUFFSZ];
	int buffsz;
	struct Msg_t* prev;
	struct Msg_t* next;
	volatile int avail;
}Msg;

typedef struct __attribute__((__packed__)) Queue_t{
	Msg* head;
	Msg* tail;
	volatile int length;
}Queue;


/*** GLOBALS ***/
Queue g_queue;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;


/*** UTILITY  FUNCTIONS ***/
void usleep(int microseconds){
	struct timespec tm;
   	tm.tv_sec = 0;
   	tm.tv_nsec = 1000*microseconds;
	nanosleep(&tm , NULL);
	return;
}

struct timespec get_timestamp() {
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return tv;
}

void print_timestamp_diff(struct timespec start, struct timespec end){
    printf("Time difference :  %.5f seconds\n",
           ((double)end.tv_sec + 1.0e-9*end.tv_nsec) - 
           ((double)start.tv_sec + 1.0e-9*start.tv_nsec));
}


/*** QUEUE FUNCTIONS ***/
int enqueue(Queue* queue, Msg* msg, int thread_id, int iter){
	if (NULL == msg){
		return 0;
	}
	pthread_mutex_lock( &lock );
	
	if (NULL != queue->tail ){
		queue->tail->next = msg;
	}
	if (NULL == queue->head){
		queue->head = msg;
	}
	msg->prev = queue->tail;
	msg->next = NULL;
	queue->tail = msg;
	queue->length += 1;	
	pthread_mutex_unlock(&lock);
	return 1;
}

Msg* dequeue(Queue* queue, int thread_id, int iter){
	Msg* msg = NULL;

	if (pthread_mutex_trylock( &lock ) != 0){
		return NULL;
	}

	if (queue->length == 0 || NULL == queue->head){
		pthread_mutex_unlock(&lock);
		return NULL;
	}
	queue->length -= 1;	
	msg = queue->head;
	assert(msg != NULL);
	
	queue->head = msg->next;
	msg->prev = NULL;
	msg->next = NULL;

	pthread_mutex_unlock(&lock);
	return msg;
}


void initialize_queue(Queue* queue){
	queue->head = NULL;
	queue->tail = NULL;
	queue->length = 0;
	return;
}


void deintialize_queue(Queue* queue){
	queue->head = NULL;
	queue->tail = NULL;
	queue->length = 0;
	return;
}


/*** MEM BUFFER FUNCTIONS ***/
typedef struct __attribute__((__packed__)) MemBuffer_t{
	Msg* buffer;
	int buffsz;
}MemBuffer;

MemBuffer* allocate_mem_buffer(int buffsz){
	MemBuffer* membuf =  (MemBuffer*) malloc(sizeof(MemBuffer));
	if(NULL == membuf){
		return NULL;
	}
	membuf->buffer= (Msg*) calloc(buffsz, sizeof(Msg));
	if (NULL == membuf->buffer){
		free(membuf);
		membuf = NULL;
	}
	membuf->buffsz = buffsz;
	return membuf;
}

MemBuffer* initialize_mem_buffer(MemBuffer* membuf){
	int i = 0;
	Msg* msgbuf = membuf->buffer;
	for (i = 0; i < membuf->buffsz; ++i){
		msgbuf[i].avail = true;
		memset(msgbuf[i].buffer, 0, sizeof(char)*BUFFSZ);
		msgbuf[i].buffsz = 0;
		msgbuf[i].prev = NULL;
		msgbuf[i].next = NULL;
	}
	return membuf;
}	

void deallocate_mem_buffer(MemBuffer* membuf){
	if (NULL != membuf){
		if (NULL != membuf->buffer){
			int i = 0;
			Msg* msgbuf = membuf->buffer;
			for (i = 0; i < membuf->buffsz; ++i){
				while(msgbuf[i].avail != true){
					usleep(1);
				}
			}
			free(membuf->buffer);
			membuf->buffer = NULL;
		}	
		free(membuf);
		membuf = NULL;
	}
	return;
}

Msg* get_avail_msg(MemBuffer* membuf){
	int i;

	for(i=0; i < membuf->buffsz; ++i){
		if (true == membuf->buffer[i].avail) {
			membuf->buffer[i].avail = false;
			return &(membuf->buffer[i]);
		}
	}
	return NULL;
}

void release_msg(Msg* msg){
	msg->avail = true;
	return;
}


/*** STUB FUNCTIONS ***/
static int proc_count;
pthread_mutex_t proc_lock = PTHREAD_MUTEX_INITIALIZER;
void process_data(char *buffer, int bufferSizeInBytes){
	pthread_mutex_lock(&proc_lock);
	proc_count++;
	pthread_mutex_unlock(&proc_lock);
	return;
}


int get_external_data(char *buffer, int bufferSizeInBytes){
	static char c = 0;
	memset(buffer, c++, bufferSizeInBytes);
	return bufferSizeInBytes;
}

/*** MAIN FUNCTIONS ***/

/**
 * This thread is responsible for pulling data off of the shared data 
 * area and processing it using the process_data() API.
 */
void *reader_thread(void *arg) {
	int iter = 0;
	int thread_id = *((int *)arg);
	Msg* message = NULL;

	while(1) {
		message = dequeue(&g_queue, thread_id, iter);
		if (NULL != message){
			process_data(message->buffer, message->buffsz);		
			release_msg(message);
			iter++;
		}

		pthread_mutex_lock(&proc_lock);
		if(proc_count >= PROCESS_CNT){
			pthread_mutex_unlock(&proc_lock);
			break;
		}
		pthread_mutex_unlock(&proc_lock);
	}
	return NULL;
}


/**
 * This thread is responsible for pulling data from a device using
 * the get_external_data() API and placing it into a shared area
 * for later processing by one of the reader threads.
 */
void *writer_thread(void *arg) {
	int iter = 0;
	int thread_id = *((int *)arg);

	Msg* message = NULL;	
	MemBuffer* membuf = NULL;

	membuf = allocate_mem_buffer(MAXBUFFS);
	if(NULL == membuf){
		return NULL;
	}
	membuf = initialize_mem_buffer(membuf);

	while(iter <  PROCESS_CNT/M) {
		message = get_avail_msg(membuf);
		if (NULL == message){
			continue;
		}
		message->buffsz = get_external_data(message->buffer, sizeof(message->buffer));		
		enqueue(&g_queue, message, thread_id, iter);
		iter++;
	}
	deallocate_mem_buffer(membuf);
	return NULL;
}




int main(int argc, char **argv) {
	int i;

	initialize_queue(&g_queue);

	pthread_t reader_threads[N];
	pthread_t writer_threads[M];
	
	struct timespec start;
	struct timespec end;
		
	start = get_timestamp();
	
	int read_thread_ids[N];
	for(i = 0; i < N; i++) { 
		read_thread_ids[i] = i;
		pthread_create(&reader_threads[i], NULL, &reader_thread, (void*) &read_thread_ids[i]);
	}

	//printf("Start Writers\n");
	int write_thread_ids[M];
	for(i = 0; i < M; i++) { 
		write_thread_ids[i] = i;
		pthread_create(&writer_threads[i], NULL, &writer_thread, (void*) &write_thread_ids[i]);
	}

	//printf("Cleanup Readers\n");
	for(i = 0; i < N; i++) { 
		pthread_join(reader_threads[i], NULL);
	}

	//printf("Cleanup Writers\n");
	for(i = 0; i < M; i++) { 
		pthread_join(writer_threads[i], NULL);
	}
	end = get_timestamp();
	print_timestamp_diff(start, end);

	deintialize_queue(&g_queue);
	return 0;	
}

/*** PERF RESULTS ***/
/***
1. CentoS
	1st: semaphore (fastest)
	2nd: mutex
	3rd: condvar
2. Cygwin
	1st: mutex
	2nd: condvar
	3rd: semaphore	
3. QNX (Can't download, can't verify)
	1st: ???
	2nd: ???
	3rd: ???

***/
