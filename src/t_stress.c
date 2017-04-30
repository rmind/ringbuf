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

#define	NSEC			10 /* seconds */

static pthread_barrier_t	barrier;
static unsigned			nworkers;
static volatile bool		stop;

static ringbuf_t *		ringbuf;
static size_t			ringbuf_size, ringbuf_local_size;
__thread uint32_t		fast_random_seed = 5381;

/* Note: leave one byte for the magic byte. */
#define	RBUF_SIZE		4095
#define	MAGIC_BYTE		0x5a

static uint8_t			rbuf[RBUF_SIZE + 1];

/*
 * Simple xorshift; random() causes huge lock contention on Linux.
 */
static unsigned long
fast_random(void)
{
	uint32_t x = fast_random_seed;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	fast_random_seed = x;
	return x;
}

static size_t
generate_message(unsigned char *buf, size_t buflen)
{
	const unsigned len = fast_random() % (buflen - 2);
	unsigned i = 1, n = len;
	unsigned char cksum = 0;

	while (n--) {
		buf[i] = '!' + (fast_random() % ('~' - '!'));
		cksum ^= buf[i];
		i++;
	}
	/* Write the length last. */
	buf[i++] = cksum;
	buf[0] = len;
	return i;
}

static ssize_t
verify_message(const unsigned char *buf, unsigned id)
{
	unsigned i = 1, len = (unsigned char)buf[0];
	unsigned char cksum = 0;

	while (len--) {
		cksum ^= buf[i++];
	}
	if (buf[i] != cksum) {
		return -1;
	}
	return (unsigned)buf[0] + 2;
}

static void *
ringbuf_stress(void *arg)
{
	const unsigned id = (uintptr_t)arg;
	ringbuf_local_t *t;
	ssize_t ret;

	t = calloc(1, ringbuf_local_size);
	assert(t != NULL);

	ret = ringbuf_register(ringbuf, t);
	assert(ret == 0);

	pthread_barrier_wait(&barrier);
	while (!stop) {
		unsigned char buf[255];
		size_t len, off;

		/* Check that the buffer is never overrun. */
		assert(rbuf[RBUF_SIZE] == MAGIC_BYTE);

		if (id == 0) {
			if ((len = ringbuf_consume(ringbuf, &off)) != 0) {
				size_t rem = len;
				assert(off < RBUF_SIZE);
				while (rem) {
					ret = verify_message(&rbuf[off], id);
					assert(ret > 0);
					assert(ret <= (ssize_t)rem);
					off += ret, rem -= ret;
				}
				ringbuf_release(ringbuf, len);
			}
			sched_yield();
			continue;
		}
		len = generate_message(buf, sizeof(buf));
		if ((ret = ringbuf_acquire(ringbuf, t, len)) != -1) {
			off = (size_t)ret;
			assert(off < RBUF_SIZE);
			memcpy(&rbuf[off], buf, len);
			ringbuf_produce(ringbuf, t);
		}
		sched_yield();
	}
	free(t);
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

	/*
	 * Setup the threads.
	 */
	nworkers = sysconf(_SC_NPROCESSORS_CONF) + 1;
	thr = calloc(nworkers, sizeof(pthread_t));
	pthread_barrier_init(&barrier, NULL, nworkers);
	stop = false;

	memset(&sigalarm, 0, sizeof(struct sigaction));
	sigalarm.sa_handler = ding;
	ret = sigaction(SIGALRM, &sigalarm, NULL);
	assert(ret == 0); (void)ret;

	/*
	 * Create a ring buffer.
	 */
	ringbuf_get_sizes(&ringbuf_size, &ringbuf_local_size);
	ringbuf = malloc(ringbuf_size);
	assert(ringbuf != NULL);

	ringbuf_setup(ringbuf, RBUF_SIZE);
	memset(rbuf, MAGIC_BYTE, sizeof(rbuf));

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
	free(ringbuf);
}

int
main(int argc, char **argv)
{
	puts("stress test");
	run_test(ringbuf_stress);
	puts("ok");
	return 0;
}
