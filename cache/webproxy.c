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

#include "gfserver.h"
#include "cache-student.h"

/* note that the -n and -z parameters are NOT used for Part 1 */
/* they are only used for Part 2 */                         
#define USAGE                                                                         \
"usage:\n"                                                                            \
"  webproxy [options]\n"                                                              \
"options:\n"                                                                          \
"  -n [segment_count]  Number of segments to use (Default: 8)\n"                      \
"  -p [listen_port]    Listen port (Default: 19121)\n"                                 \
"  -t [thread_count]   Num worker threads (Default: 11, Range: 1-1219)\n"              \
"  -s [server]         The server to connect to (Default: Udacity S3 instance)\n"     \
"  -z [segment_size]   The segment size (in bytes, Default: 8192).\n"                  \
"  -h                  Show this help message\n"


/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
  {"segment-count", required_argument,      NULL,           'n'},
  {"port",          required_argument,      NULL,           'p'},
  {"thread-count",  required_argument,      NULL,           't'},
  {"server",        required_argument,      NULL,           's'},
  {"segment-size",  required_argument,      NULL,           'z'},         
  {"help",          no_argument,            NULL,           'h'},
  {"hidden",        no_argument,            NULL,           'i'}, /* server side */
  {NULL,            0,                      NULL,            0}
};

void Pthread_mutex_lock(pthread_mutex_t *mutex) {
  int rc = pthread_mutex_lock(mutex);
  assert(rc == 0);
}

void Pthread_mutex_unlock(pthread_mutex_t *mutex) {
  int rc = pthread_mutex_unlock(mutex);
  assert(rc == 0);
}

void Pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex){
  int rc = pthread_cond_wait(cond, mutex);
  assert(rc == 0);
}

void Pthread_cond_signal(pthread_cond_t *cond){
  int rc = pthread_cond_signal(cond);
  assert(rc == 0);
}

void Pthread_cond_broadcast(pthread_cond_t *cond){
  int rc = pthread_cond_broadcast(cond);
  assert(rc == 0);
}

extern ssize_t handle_with_cache(gfcontext_t *ctx, char *path, void* arg);

static gfserver_t gfs;

/* Holds pointers to shared memory segments for clean up on signal */
static steque_t shm_ref_queue;

static void _sig_handler(int signo){
  if (signo == SIGTERM || signo == SIGINT){
    /* Reclaim memory segments */
    while(!steque_isempty(&shm_ref_queue)) {
      int shmid = *((int *)steque_pop(&shm_ref_queue));
      shmctl(shmid, IPC_RMID, NULL);
    }
    gfserver_stop(&gfs);
    exit(signo);
  }
}

/* Main ========================================================= */
int main(int argc, char **argv) {
  int i;
  int option_char = 0;
  unsigned short port = 19121;
  unsigned short nworkerthreads = 3;
  unsigned int nsegments = 8;
  size_t segsize = 8192;
  char *server = "s3.amazonaws.com/content.udacity-data.com";

  /* disable buffering on stdout so it prints immediately */
  setbuf(stdout, NULL);

  steque_init(&shm_ref_queue);

  if (signal(SIGINT, _sig_handler) == SIG_ERR) {
    fprintf(stderr,"Can't catch SIGINT...exiting.\n");
    exit(SERVER_FAILURE);
  }

  if (signal(SIGTERM, _sig_handler) == SIG_ERR) {
    fprintf(stderr,"Can't catch SIGTERM...exiting.\n");
    exit(SERVER_FAILURE);
  }

  /* Parse and set command line arguments */
  while ((option_char = getopt_long(argc, argv, "ixls:t:hn:p:z:", gLongOptions, NULL)) != -1) {
    switch (option_char) {
      default:
        fprintf(stderr, "%s", USAGE);
        exit(__LINE__);
      case 'p': // listen-port
        port = atoi(optarg);
        break;
      case 'n': // segment count
        nsegments = atoi(optarg);
        break;   
      case 's': // file-path
        server = optarg;
        break;                                          
      case 'z': // segment size
        segsize = atoi(optarg);
        break;
      case 't': // thread-count
        nworkerthreads = atoi(optarg);
        break;
      case 'i':
      case 'x':
      case 'l':
        break;
      case 'h': // help
        fprintf(stdout, "%s", USAGE);
        exit(0);
        break;
    }
  }

  if (segsize < 128) {
    fprintf(stderr, "Invalid segment size\n");
    exit(__LINE__);
  }

  if (!server) {
    fprintf(stderr, "Invalid (null) server name\n");
    exit(__LINE__);
  }

  if (port < 1024) {
    fprintf(stderr, "Invalid port number\n");
    exit(__LINE__);
  }

  if (nsegments < 1) {
    fprintf(stderr, "Must have a positive number of segments\n");
    exit(__LINE__);
  }

  if ((nworkerthreads < 1) || (nworkerthreads > 1219)) {
    fprintf(stderr, "Invalid number of worker threads\n");
    exit(__LINE__);
  }

  /* Setup queue */
  steque_t work_queue;
	steque_init(&work_queue);

  // Initialize shared memory set-up here
  shm_msg shm_segs[nsegments]; 
  key_t key;
  int shm_id;
  for (int i = 0; i < nsegments; i++){
     key = ftok(argv[0], i);
     shm_id = shmget(key, segsize, 0666 | IPC_CREAT);
     shm_segs[i].shmid = shm_id;
     shm_segs[i].shm_size = segsize;
     steque_enqueue(&work_queue, &shm_segs[i]);
     steque_enqueue(&shm_ref_queue, &(shm_segs[i].shmid));

     semaphores *read_write_sems = (semaphores *)shmat(shm_id, (void *)0, 0);
     sem_init(&(read_write_sems->read), 1, 0);
     sem_init(&(read_write_sems->write), 1, 1);
     shmdt(read_write_sems);
  }

  // Initialize server structure here
  gfserver_init(&gfs, nworkerthreads);

  // Set server options here
  gfserver_setopt(&gfs, GFS_MAXNPENDING, 801);
  gfserver_setopt(&gfs, GFS_WORKER_FUNC, handle_with_cache);
  gfserver_setopt(&gfs, GFS_PORT, port);

  pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t cons_cond = PTHREAD_COND_INITIALIZER;

  /* Data needed by workers */
  work_arg warg = {
    .work_queue = &work_queue,
    .queue_lock = &queue_lock,
    .cons_cond = &cons_cond,
	};

  // Set up arguments for worker here
  for(i = 0; i < nworkerthreads; i++) {
    warg.mtype = i + 1;
    gfserver_setopt(&gfs, GFS_WORKER_ARG, i, &warg);
  }
  
  // Invoke the framework - this is an infinite loop and shouldn't return
  gfserver_serve(&gfs);
}
