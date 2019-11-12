/*
 *  This file is for use by students to define anything they wish.  It is used by the proxy cache implementation
 */
 #ifndef __CACHE_STUDENT_H__
 #define __CACHE_STUDENT_H__

 #include "steque.h"
 
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <semaphore.h> 
#include "gfserver.h"
#include <assert.h>

#define MAX_CACHE_REQUEST_LEN 5041

#define REQ_QUEUE_KEY 1
#define RESP_QUEUE_KEY REQ_QUEUE_KEY + 1

extern char* gfstatus_t_to_str(gfstatus_t status);

extern void Pthread_mutex_lock(pthread_mutex_t *mutex);
extern void Pthread_mutex_unlock(pthread_mutex_t *mutex);
extern void Pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
extern void Pthread_cond_signal(pthread_cond_t *cond);
extern void Pthread_cond_broadcast(pthread_cond_t *cond);

typedef struct handle_with_cache_arg {
    long mtype;

} handle_with_cache_arg;

typedef struct cache_req_msg {
    long mtype;
	char mtext[MAX_CACHE_REQUEST_LEN];

	int shm_id;
	ssize_t shm_size;
} cache_req_msg ;

typedef struct cache_resp_msg {
	long mtype;

	gfstatus_t status;
	ssize_t file_size;
} cache_resp_msg;

typedef struct semaphores {
	sem_t read;
	sem_t write;

} semaphores ;

typedef struct work_arg {
    long mtype;
	steque_t *work_queue;
	pthread_mutex_t *queue_lock;
	pthread_cond_t *cons_cond;
} work_arg ;

typedef struct shm_msg {
    int shmid;
    size_t shm_size;
} shm_msg;
 
 #endif // __CACHE_STUDENT_H__