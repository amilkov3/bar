#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <sys/signal.h>
#include <printf.h>
#include <curl/curl.h>

#include "cache-student.h"
#include "shm_channel.h"
#include "simplecache.h"

#if !defined(CACHE_FAILURE)
#define CACHE_FAILURE (-1)
#endif // CACHE_FAILURE


static void _sig_handler(int signo){
	if (signo == SIGTERM || signo == SIGINT){
		// you should do IPC cleanup here
		int resp_qid, req_qid;
		if ((req_qid = msgget(REQ_QUEUE_KEY, IPC_CREAT | 0666)) == -1 || 
			(resp_qid = msgget(RESP_QUEUE_KEY, IPC_CREAT | 0666)) == -1)  {
			perror ("Failed to get request or response queue file descriptors");
			exit (1);
		}
		/* Clean up */
		msgctl(req_qid, IPC_RMID, NULL);
        msgctl(resp_qid, IPC_RMID, NULL);
		exit(signo);
	}
}

extern unsigned long int cache_delay;

#define USAGE                                                                 \
"usage:\n"                                                                    \
"  simplecached [options]\n"                                                  \
"options:\n"                                                                  \
"  -c [cachedir]       Path to static files (Default: ./)\n"                  \
"  -t [thread_count]   Thread count for work queue (Default is 7, Range is 1-31415)\n"      \
"  -d [delay]          Delay in simplecache_get (Default is 0, Range is 0-5000 (ms)\n "	\
"  -h                  Show this help message\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
  {"cachedir",           required_argument,      NULL,           'c'},
  {"nthreads",           required_argument,      NULL,           't'},
  {"help",               no_argument,            NULL,           'h'},
  {"hidden",			 no_argument,			 NULL,			 'i'}, /* server side */
  {"delay", 			 required_argument,		 NULL, 			 'd'}, // delay.
  {NULL,                 0,                      NULL,             0}
};

void Usage() {
  fprintf(stdout, "%s", USAGE);
}

char* gfstatus_t_to_str(gfstatus_t status) {
    if (status == GF_OK) {
        return "GF_OK";
    } else if (status == GF_FILE_NOT_FOUND) {
        return "GF_FILE_NOT_FOUND";
    } else {
        return "GF_ERROR";
    }
}

void* work(void *arg){
	work_arg *warg = (work_arg *) arg;
	cache_req_msg *cache_req;
	int fd, resp_qid;
	size_t filelen;
	semaphores *read_write_sems;

	while (1) {
		/* Pull item off queue */
		pthread_mutex_lock(warg->queue_lock);
		while (steque_isempty(warg->work_queue)){
			pthread_cond_wait(warg->cons_cond, warg->queue_lock);
		}
		cache_req = steque_pop(warg->work_queue);
		pthread_mutex_unlock(warg->queue_lock);

		cache_resp_msg cache_resp;
		cache_resp.mtype = cache_req->mtype;

		cache_resp.status = GF_FILE_NOT_FOUND;
		cache_resp.file_size = 0;

		/* Get file descriptor from queue */
		if ((fd = simplecache_get(cache_req->mtext)) != -1) {
			struct stat st;
			fstat(fd, &st);
			filelen = st.st_size;

			cache_resp.status = GF_OK;
			cache_resp.file_size = filelen;
		}

		if ((resp_qid = msgget(RESP_QUEUE_KEY, 0666)) == -1) {
			perror("Receiving message from request queue failed");
		}

		printf("Sending response back - status: %s, file_size: %ld, thread_id: %ld\n",
			gfstatus_t_to_str(cache_resp.status),
			cache_resp.file_size,
			cache_resp.mtype
		);

		/* Notify of impending response via resp queue */
		if (msgsnd(resp_qid, &cache_resp, sizeof(cache_resp_msg), 0) == -1) {
			perror("Failed to send to response queue");
		}

		/* Cache not present. Abort early */
		if (fd <= 0) {
			free(cache_req);
			continue;
		}

		/* Attach memory segment via which file contents will be transmitted */
		if ((read_write_sems = shmat(cache_req->shm_id, (void *) 0, 0)) == (semaphores *) -1) {
			perror("Failed to attach memory segment");
		}

		/* Begin transmitting response */
		size_t seg_data_size = cache_req->shm_size - sizeof(semaphores);
		size_t bytes_sent = 0;
		/* Process shared memory segment sized chunks of the file at a time */
		while (bytes_sent < filelen) {
			
			// Determine write length
			size_t write_len; 
			if ((filelen - bytes_sent) <= seg_data_size){
				write_len = filelen - bytes_sent;
			} else {
				write_len = seg_data_size;
			}

			// Lock write semaphore
			sem_wait(&(read_write_sems)->write);

			// Pointer to the actual data section of shared memory
			char *data = (char *)read_write_sems + sizeof(semaphores);
			memset(data, 0, seg_data_size);

			size_t total_bytes = 0;
			ssize_t bytes_read = 0;

			// Write as many file chunks as will fit in shared memory segment
			while (total_bytes < write_len){
				if ((bytes_read = pread(fd, data, write_len - total_bytes, bytes_sent + total_bytes)) < 0) {
					perror("Failed to read from shared memory");
				}
				total_bytes += bytes_read; 
				/*printf("%ld bytes written to shared memory. %ld bytes of %ld total - path: %s\n",
					bytes_read,
					bytes_sent,
					filelen,
					cache_req->mtext
				);*/
			}
			bytes_sent += total_bytes;
			printf("%ld bytes written to shared memory. %ld bytes of %ld total - path: %s\n",
				total_bytes,
				bytes_sent,
				filelen,
				cache_req->mtext
			);

			// Unlock read semaphore
			sem_post(&(read_write_sems->read));
		}

		/* Detach memory segment */
		shmdt((void *) read_write_sems);

		/* Free request struct memory */
		free(cache_req);
	}
}


int main(int argc, char **argv) {
	int nthreads = 7;
	char *cachedir = "locals.txt";
	char option_char;

	/* disable buffering to stdout */
	setbuf(stdout, NULL);

	while ((option_char = getopt_long(argc, argv, "id:c:hlxt:", gLongOptions, NULL)) != -1) {
		switch (option_char) {
			default:
				Usage();
				exit(1);
			case 'c': //cache directory
				cachedir = optarg;
				break;
			case 'h': // help
				Usage();
				exit(0);
				break;    
			case 't': // thread-count
				nthreads = atoi(optarg);
				break;
			case 'd':
				cache_delay = (unsigned long int) atoi(optarg);
			case 'i': // server side usage
			case 'l': // experimental
			case 'x': // experimental
				break;
		}
	}

	if (cache_delay > 5000) {
		fprintf(stderr, "Cache delay must be less than 5000 (ms)\n");
		exit(__LINE__);
	}

	if ((nthreads>31415) || (nthreads < 1)) {
		fprintf(stderr, "Invalid number of threads\n");
		exit(__LINE__);
	}

	if (SIG_ERR == signal(SIGINT, _sig_handler)){
		fprintf(stderr,"Unable to catch SIGINT...exiting.\n");
		exit(CACHE_FAILURE);
	}

	if (SIG_ERR == signal(SIGTERM, _sig_handler)){
		fprintf(stderr,"Unable to catch SIGTERM...exiting.\n");
		exit(CACHE_FAILURE);
	}

	// Initialize cache
	simplecache_init(cachedir);

	// cache code gos here

	/* Init worker steque */
	steque_t work_queue;
	steque_init(&work_queue);

	int resp_qid, req_qid;

	/* Get req and response queue descriptors. Create if they don't exist */
	if ((req_qid = msgget (REQ_QUEUE_KEY, IPC_CREAT | 0666)) == -1 || 
		(resp_qid = msgget (RESP_QUEUE_KEY, IPC_CREAT | 0666)) == -1)  {
        perror ("Failed to get request or response queue file descriptors");
    }

	pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
  	pthread_cond_t cons_cond = PTHREAD_COND_INITIALIZER;

	/* Stores data needed by workers */
	work_arg warg = {
		.work_queue = &work_queue,
		.queue_lock = &queue_lock,
    	.cons_cond = &cons_cond,
	};

	/* Create and init thread pool */
	pthread_t pool[nthreads];
  	for (int i = 0; i < nthreads; i++){
		if (pthread_create(&pool[i], NULL, work, &warg) != 0){
			fprintf(stderr, "Error creating thread # %d", i);
			exit(CACHE_FAILURE);
		}
	}

	while (1) {
		cache_req_msg *cache_req = (cache_req_msg *) malloc(sizeof(cache_req_msg));

		/* Receive message from queue */
		if (msgrcv(req_qid, cache_req, sizeof(cache_req_msg), 0, 0) == -1){
			perror("Receiving message from request queue failed");
		}

		printf("Received request - path: %s, thread id: %ld, shm_id: %d, shmd_size: %ld\n", 
			cache_req->mtext,
			cache_req->mtype,
			cache_req->shm_id,
			cache_req->shm_size
		);

		/* Enqueue request for processing */
		pthread_mutex_lock(warg.queue_lock);
		steque_enqueue(&work_queue, cache_req);
		pthread_mutex_unlock(warg.queue_lock);
		pthread_cond_signal(warg.cons_cond);
	}

	// Won't execute
	return 0;
}
