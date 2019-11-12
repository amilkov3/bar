

#include "gfserver.h"
#include "proxy-student.h"

#define BUFSIZE (1219)

typedef struct gfcurlctx_t {
	gfcontext_t *ctx;
	size_t bytes_recved;
	char *resp_buffer;
} gfcurlctx_t;

size_t handle_resp_chunk(void *buffer, size_t sz, size_t nmemb, void *ctx) {
	gfcurlctx_t *curl_ctx = (gfcurlctx_t *) ctx;

	printf("Callback invoked with chunk size %ld bytes. %ld bytes sent so far\n", nmemb, curl_ctx->bytes_recved);

	/* Total bytes */
	size_t num_bytes = sz * nmemb;

	/* Realloc space to read in response chunk */
	curl_ctx->resp_buffer = Realloc(curl_ctx->resp_buffer, curl_ctx->bytes_recved + num_bytes + 1);

	/* Copy bytes to resp_buffer*/
	memcpy(&(curl_ctx->resp_buffer[curl_ctx->bytes_recved]), buffer, num_bytes);
	/* Update num bytes read */
	curl_ctx->bytes_recved += num_bytes;
	/* 0 terminate resp_buffer */
	curl_ctx->resp_buffer[curl_ctx->bytes_recved] = 0;

	return num_bytes;
}

/*
 * Replace with an implementation of handle_with_curl and any other
 * functions you may need.
 */
ssize_t handle_with_curl(gfcontext_t *ctx, const char *path, void* arg){
	char url[BUFSIZE];
	CURL *client;
	CURLcode res;
	CURLcode get_content_length;
	CURLcode get_resp_code;
	long resp_code;
	double content_length;

	/* Init context for curl handler */
	gfcurlctx_t curl_ctx = {
		.ctx = ctx,
		.bytes_recved = 0L,
		.resp_buffer = NULL,
	};
	
	/* Make URL */
	strncpy(url, (char *) arg, BUFSIZE);
	strncat(url, path, BUFSIZE);

	printf("Received curl request with path %s\n", url);

	/* Set req params */
	client = curl_easy_init();
	curl_easy_setopt(client, CURLOPT_URL, url);
	curl_easy_setopt(client, CURLOPT_WRITEFUNCTION, handle_resp_chunk);
	curl_easy_setopt(client, CURLOPT_WRITEDATA, &curl_ctx);
	curl_easy_setopt(client, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(client, CURLOPT_FOLLOWLOCATION, 1L);

	res = curl_easy_perform(client);

	/* Call itself failed. Return server error */
	if (res != CURLE_OK) {
		printf("%d from %s - Error: %s\n", (int) res, path, curl_easy_strerror(res));
		return SERVER_FAILURE;
	}
	 
	/* Used as file size in gfs header */
	if ((get_content_length = curl_easy_getinfo(client, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &content_length))) {
		fprintf(stderr, "Error getting Content Length header: %s", curl_easy_strerror(get_content_length));
		return SERVER_FAILURE;
	}

	/* Used to determine what header error to send */
 	if ((get_resp_code = curl_easy_getinfo(client, CURLINFO_RESPONSE_CODE, &resp_code))){
		fprintf(stderr, "Error getting response code: %s", curl_easy_strerror(get_content_length));
		return SERVER_FAILURE;
	}

	printf("Processing CURL response - Content Length: %f, response code %ld\n", content_length, resp_code);

	/* Send header and return early */
	if (resp_code >= 400 && resp_code < 500) {
		return gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);
	} else if (resp_code > 500) {
		return gfs_sendheader(ctx, GF_ERROR, 0);
	}

	/* Begin relaying back successful CURL response server data */
	gfs_sendheader(ctx, GF_OK, content_length);

	/* Transmit body */
	int bytes_sent = 0;
	int send_chunk_len;
	while (bytes_sent < curl_ctx.bytes_recved) {
		send_chunk_len = gfs_send(ctx, curl_ctx.resp_buffer, curl_ctx.bytes_recved);
		if (send_chunk_len != curl_ctx.bytes_recved) {
			fprintf(stderr, "Attempted to send %zd bytes but actually sent %d bytes\n", curl_ctx.bytes_recved, send_chunk_len);
			return SERVER_FAILURE;
		}
		bytes_sent += send_chunk_len;
	}

	printf("Finished sending %s. Sent %d bytes\n", path, bytes_sent);

	/* Clean up */
	curl_easy_cleanup(client);
	free(curl_ctx.resp_buffer);

	return bytes_sent;
}


/*
 * We provide a dummy version of handle_with_file that invokes handle_with_curl
 * as a convenience for linking.  We recommend you simply modify the proxy to
 * call handle_with_curl directly.
 */
ssize_t handle_with_file(gfcontext_t *ctx, const char *path, void* arg){
	return handle_with_curl(ctx, path, arg);
}	
