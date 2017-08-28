/*
 * Copyright (c) 2017 Mindaugas Rasiukevicius <rmind at netbsd org>
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
#include <limits.h>
#include <signal.h>
#include <pthread.h>
#include <assert.h>
#include <errno.h>
#include <err.h>

#include "ringbuf.h"
#include "utils.h"

static unsigned			nsec = 10; /* seconds */

static pthread_barrier_t	barrier;
static unsigned			nworkers;
static volatile bool		stop;

static ringbuf_t *		ringbuf;
static size_t			ringbuf_obj_size;
__thread uint32_t		fast_random_seed = 5381;

#define	RBUF_SIZE		(512)
#define	MAGIC_BYTE		(0x5a)

/* Note: leave one byte for the magic byte. */
static uint8_t			rbuf[RBUF_SIZE + 1];

/*
 * Simple xorshift; random() causes huge lock contention on Linux/glibc,
 * which would "hide" the possible race conditions.
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

/*
 * Generate a random message of a random length (up to the given size)
 * and simple XOR based checksum.  The first byte is reserved for the
 * message length and the last byte is reserved for a checksum.
 */
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
	/*
	 * Write the length and checksum last, trying to exploit a
	 * possibility of a race condition.  NOTE: depending on an
	 * architecture, might want to try a memory barrier here.
	 */
	buf[i++] = cksum;
	buf[0] = len;
	return i;
}

/*
 * Take an arbitrary message of a variable length and verify its checksum.
 */
static ssize_t
verify_message(const unsigned char *buf)
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
	ringbuf_worker_t *w;

	w = ringbuf_register(ringbuf, id);
	assert(w != NULL);

	/*
	 * There are NCPU threads concurrently generating and producing
	 * random messages and a single consumer thread (ID 0) verifying
	 * and releasing the messages.
	 */

	pthread_barrier_wait(&barrier);
	while (!stop) {
		unsigned char buf[MIN((1 << CHAR_BIT), RBUF_SIZE)];
		size_t len, off;
		ssize_t ret;

		/* Check that the buffer is never overrun. */
		assert(rbuf[RBUF_SIZE] == MAGIC_BYTE);

		if (id == 0) {
			if ((len = ringbuf_consume(ringbuf, &off)) != 0) {
				size_t rem = len;
				assert(off < RBUF_SIZE);
				while (rem) {
					ret = verify_message(&rbuf[off]);
					assert(ret > 0);
					assert(ret <= (ssize_t)rem);
					off += ret, rem -= ret;
				}
				ringbuf_release(ringbuf, len);
			}
			continue;
		}
		len = generate_message(buf, sizeof(buf) - 1);
		if ((ret = ringbuf_acquire(ringbuf, w, len)) != -1) {
			off = (size_t)ret;
			assert(off < RBUF_SIZE);
			memcpy(&rbuf[off], buf, len);
			ringbuf_produce(ringbuf, w);
		}
	}
	pthread_barrier_wait(&barrier);
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
	ringbuf_get_sizes(nworkers, &ringbuf_obj_size, NULL);
	ringbuf = malloc(ringbuf_obj_size);
	assert(ringbuf != NULL);

	ringbuf_setup(ringbuf, nworkers, RBUF_SIZE);
	memset(rbuf, MAGIC_BYTE, sizeof(rbuf));

	/*
	 * Spin the test.
	 */
	alarm(nsec);

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
	if (argc >= 2) {
		nsec = (unsigned)atoi(argv[1]);
	}
	puts("stress test");
	run_test(ringbuf_stress);
	puts("ok");
	return 0;
}
