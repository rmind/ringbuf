#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "ringbuf.h"

static size_t	ringbuf_size, ringbuf_local_size;

static void
test_wraparound(void)
{
	const size_t n = 1000;
	ringbuf_t *r = malloc(ringbuf_size);
	ringbuf_local_t *t = malloc(ringbuf_local_size);
	size_t len, woff;
	ssize_t off;

	/* Size n, but only (n - 1) can be produced at a time. */
	ringbuf_setup(r, n);
	ringbuf_register(r, t);

	/* Produce (n / 2 + 1) and then attempt another (n / 2 - 1). */
	off = ringbuf_acquire(r, t, n / 2 + 1);
	assert(off == 0);
	ringbuf_produce(r, t);

	off = ringbuf_acquire(r, t, n / 2 - 1);
	assert(off == -1);

	/* Consume (n / 2 + 1) bytes. */
	len = ringbuf_consume(r, &woff);
	assert(len == (n / 2 + 1) && woff == 0);
	ringbuf_release(r, len);

	/* All consumed, attempt (n / 2 + 1) now. */
	off = ringbuf_acquire(r, t, n / 2 + 1);
	assert(off == -1);

	/* However, wraparound can be successful with (n / 2). */
	off = ringbuf_acquire(r, t, n / 2);
	assert(off == 0);
	ringbuf_produce(r, t);

	/* Consume (n / 2) bytes. */
	len = ringbuf_consume(r, &woff);
	assert(len == (n / 2) && woff == 0);
	ringbuf_release(r, len);

	free(r);
	free(t);
}

static void
test_multi(void)
{
	ringbuf_t *r = malloc(ringbuf_size);
	ringbuf_local_t *t = malloc(ringbuf_local_size);
	size_t len, woff;
	ssize_t off;

	ringbuf_setup(r, 3);
	ringbuf_register(r, t);

	/*
	 * Produce 2 bytes.
	 */

	off = ringbuf_acquire(r, t, 1);
	assert(off == 0);
	ringbuf_produce(r, t);

	off = ringbuf_acquire(r, t, 1);
	assert(off == 1);
	ringbuf_produce(r, t);

	off = ringbuf_acquire(r, t, 1);
	assert(off == -1);

	/*
	 * Consume 2 bytes.
	 */
	len = ringbuf_consume(r, &woff);
	assert(len == 2 && woff == 0);
	ringbuf_release(r, len);

	len = ringbuf_consume(r, &woff);
	assert(len == 0);

	/*
	 * Produce another 2 with wrap-around.
	 */

	off = ringbuf_acquire(r, t, 2);
	assert(off == -1);

	off = ringbuf_acquire(r, t, 1);
	assert(off == 2);
	ringbuf_produce(r, t);

	off = ringbuf_acquire(r, t, 1);
	assert(off == 0);
	ringbuf_produce(r, t);

	off = ringbuf_acquire(r, t, 1);
	assert(off == -1);

	/*
	 * Consume 1 byte at the end and 1 byte at the beginning.
	 */

	len = ringbuf_consume(r, &woff);
	assert(len == 1 && woff == 2);
	ringbuf_release(r, len);

	len = ringbuf_consume(r, &woff);
	assert(len == 1 && woff == 0);
	ringbuf_release(r, len);

	free(r);
	free(t);
}

int
main(void)
{
	ringbuf_get_sizes(&ringbuf_size, &ringbuf_local_size);
	test_wraparound();
	test_multi();
	puts("ok");
	return 0;
}
