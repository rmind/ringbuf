/*
 * Copyright (c) 2015 Mindaugas Rasiukevicius <rmind at netbsd org>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <assert.h>
#include <errno.h>
#include <err.h>

#include "ringbuf.h"

#define	NSEC			10

static pthread_barrier_t	barrier;
static unsigned			nworkers;
static volatile bool		stop;

static int			fd;
static ringbuf_t *		ringbuf;
static uint64_t			written[512];

static const char		logline[] =
    "10.0.0.1 - - [29/Apr/2016:17:02:50 +0100] "
    "\"GET /some-random-path/payload/1.ts HTTP/1.1\" 206 1048576 "
    "\"-\" \"curl/7.29.0\" \"-\"\n";
static const size_t		logbytes = sizeof(logline) - 1;
static uint8_t			rbuf[4096];

static void *
write_test(void *arg)
{
	const unsigned id = (uintptr_t)arg;
	uint64_t total_bytes = 0;

	written[id] = 0;
	pthread_barrier_wait(&barrier);
	while (!stop) {
		char buf[logbytes + 1];
		ssize_t ret;

		memcpy(buf, logline, logbytes);
		ret = write(fd, buf, logbytes);
		assert(ret == (ssize_t)logbytes);
		total_bytes += ret;
	}
	written[id] = total_bytes;
	pthread_exit(NULL);
	return NULL;
}

static void *
ringbuf_test(void *arg)
{
	const unsigned id = (uintptr_t)arg;
	uint64_t total_bytes = 0;
	int rv;

	rv = ringbuf_register(ringbuf);
	assert(rv == 0); (void)rv;

	written[id] = 0;
	pthread_barrier_wait(&barrier);
	while (!stop) {
		size_t len, off;
		ssize_t ret;

		if (id == 0) {
			if ((len = ringbuf_consume(ringbuf, &off)) != 0) {
				size_t rem = len;
				assert(off < sizeof(rbuf));
				while (rem) {
					ret = write(fd, &rbuf[off], rem);
					off += ret, rem -= ret;
					assert(ret != -1);
				}
				ringbuf_release(ringbuf, len);
				total_bytes += len;
			}
			continue;
		}
		if ((ret = ringbuf_acquire(ringbuf, logbytes)) != -1) {
			off = (size_t)ret;
			assert(off < sizeof(rbuf));
			memcpy(&rbuf[off], logline, logbytes);
			ringbuf_produce(ringbuf);
		}
	}
	written[id] = total_bytes;
	pthread_exit(NULL);
	return NULL;
}

static void
ding(int sig)
{
	(void)sig;
	stop = true;
}

static void
run_test(void *func(void *))
{
	struct sigaction sigalarm;
	pthread_t *thr;
	int ret;

	srandom(1);
	nworkers = sysconf(_SC_NPROCESSORS_CONF) + 1;

	/*
	 * Setup the threads
	 */
	thr = malloc(sizeof(pthread_t) * nworkers);
	pthread_barrier_init(&barrier, NULL, nworkers);
	stop = false;

	memset(&sigalarm, 0, sizeof(struct sigaction));
	sigalarm.sa_handler = ding;
	ret = sigaction(SIGALRM, &sigalarm, NULL);
	assert(ret == 0); (void)ret;

	/*
	 * Open the log file.
	 */
	fd = open("test.log", O_RDWR | O_CREAT | O_TRUNC | O_APPEND, 0644);
	assert(fd != -1);

	/*
	 * Create a ring buffer;
	 */
	ringbuf = ringbuf_create(sizeof(rbuf));
	assert(ringbuf != NULL);

	/*
	 * Spin the benchmark.
	 */
	alarm(NSEC);

	for (unsigned i = 0; i < nworkers; i++) {
		if ((errno = pthread_create(&thr[i], NULL,
		    func, (void *)(uintptr_t)i)) != 0) {
			err(EXIT_FAILURE, "pthread_create");
		}
	}
	for (unsigned i = 0; i < nworkers; i++) {
		pthread_join(thr[i], NULL);
	}
	pthread_barrier_destroy(&barrier);
	close(fd);

	uint64_t total_written = 0;
	for (unsigned i = 0; i < nworkers; i++) {
		total_written += written[i];
	}
	printf("%"PRIu64" MB/sec\n", total_written / 1024 / 1024 / NSEC);
}

int
main(int argc, char **argv)
{
	if (argc < 2) {
		return -1;
	}
	switch (atoi(argv[1])) {
	case 0:
		puts("concurrent write");
		run_test(write_test);
		break;
	case 1:
		puts("ringbuf + writer");
		run_test(ringbuf_test);
		break;
	}
	return 0;
}
