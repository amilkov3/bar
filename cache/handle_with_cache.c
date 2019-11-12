#include "gfserver.h"
#include "cache-student.h"

#define BUFSIZE (1219)

/* For logging */
char* gfstatus_t_to_str(gfstatus_t status) {
    if (status == GF_OK) {
        return "GF_OK";
    } else if (status == GF_FILE_NOT_FOUND) {
        return "GF_FILE_NOT_FOUND";
    } else {
        return "GF_ERROR";
    }
}

ssize_t handle_with_cache(gfcontext_t *ctx, const char *path, void* arg){
	int req_qid, resp_qid, return_value;
	shm_msg *shm;
	cache_resp_msg resp;
	semaphores *read_write_sems;

	work_arg *warg = (work_arg *) arg;
	
	/* Spin until request queue is online */
	while ((req_qid = msgget(REQ_QUEUE_KEY, 0666)) == -1) {
		printf("Request queue with key %d not found. Waiting 1 second\n", REQ_QUEUE_KEY);
		sleep(1);
	}

	cache_req_msg cache_req = {
		.mtype = warg->mtype,
	};

	/* Create URL */
	strncpy(cache_req.mtext, path, MAX_CACHE_REQUEST_LEN);

	/* Get a shared memory segment */
	pthread_mutex_lock(warg->queue_lock);
	while (steque_isempty(warg->work_queue)) {
		pthread_cond_wait(warg->cons_cond, warg->queue_lock);
	}
	shm = (shm_msg *) steque_pop(warg->work_queue);
	pthread_mutex_unlock(warg->queue_lock);

	cache_req.shm_id = shm->shmid;
	cache_req.shm_size = shm->shm_size;

	printf("Sending cache request - path: %s, threadid: %ld, shm_id: %d, shmd_size: %ld\n",
		cache_req.mtext,
		cache_req.mtype,
		cache_req.shm_id,
		cache_req.shm_size
	);

	/* Send request to simplecached */
	if (msgsnd(req_qid, &cache_req, sizeof(cache_req_msg), 0) == -1) {
		perror("msgsnd");
	}

	/* Spin until response queue is online */
	while ((resp_qid = msgget(RESP_QUEUE_KEY, 0666)) == -1) {
		printf("Response queue with key %d not found. Waiting 1 second\n", (int) RESP_QUEUE_KEY);
		sleep(1);
	}

	/* Receive response from simplecached */
	if (msgrcv(resp_qid, &resp, sizeof(cache_req_msg), warg->mtype, 0) == -1) { 
        perror("msgrcv");
	}

    printf("Received cache response - mtype = %ld, status = %s, file_size = %zu\r\n", resp.mtype, gfstatus_t_to_str(resp.status), resp.file_size);

	/* Send header and return early */
	if (resp.status == GF_FILE_NOT_FOUND || resp.status == GF_ERROR) {
		fprintf(stderr, "Cache response with error. Sending error header and returning early\n");
		return_value = gfs_sendheader(ctx, resp.status, 0);
	} else {

		/* Send success header */
		gfs_sendheader(ctx, GF_OK, resp.file_size);

		/* Attach shared memory segment into process's address space */
		if ((read_write_sems = shmat(shm->shmid, (void *)0, 0)) == (semaphores *)(-1)) {
			perror("shmat");
		}

		size_t seg_data_size = shm->shm_size - sizeof(semaphores);
		ssize_t bytes_sent = 0;
		while (bytes_sent < resp.file_size) {

			/* Determine size to read */
			size_t read_len;
			if ((resp.file_size - bytes_sent) <= seg_data_size) {
				read_len = resp.file_size - bytes_sent;
			}  else {
				read_len = seg_data_size;
			}
			
			/* Lock read semaphore */
			sem_wait(&(read_write_sems)->read);

			/* Pointer to begninning of actual data in segment */
			char *data = (char *)read_write_sems + sizeof(semaphores);

			size_t total_bytes = 0;
			ssize_t bytes_sent_seg = 0;
			while (total_bytes < read_len){
				if ((bytes_sent_seg = gfs_send(ctx, data + total_bytes, read_len - total_bytes)) < 0) {
					perror("Sending response back failed");
				}
				total_bytes += bytes_sent_seg;
				/*printf("%ld bytes read from shared memory and sent. %ld bytes of %ld total - path: %s\n",
					bytes_sent_seg,
					bytes_sent,
					resp.file_size,
					path
				);*/
			}
			bytes_sent += total_bytes;
			printf("%ld bytes read from shared memory and sent. %ld bytes of %ld total - path: %s\n",
				total_bytes,
				bytes_sent,
				resp.file_size,
				path
			);

			/* Unlock write semaphore */
			sem_post(&(read_write_sems->write));

		}

		return_value = bytes_sent;
		/* Detach memory segment */
		shmdt((void *) read_write_sems);
	}

	/* Return memory segment to queue */
	pthread_mutex_lock(warg->queue_lock);
	steque_enqueue(warg->work_queue, shm);
	pthread_mutex_unlock(warg->queue_lock);
	pthread_cond_signal(warg->cons_cond);

	return return_value;

}

