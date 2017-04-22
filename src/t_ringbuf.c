#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "ringbuf.h"

static void
test_wraparound(void)
{
	const size_t n = 1000;
	ringbuf_t *r;
	size_t len, woff;
	ssize_t off;

	/* Size n, but only (n - 1) can be produced at a time. */
	r = ringbuf_create(n);
	assert(r != NULL);
	ringbuf_register(r);

	/* Produce (n / 2 + 1) and then attempt another (n / 2 - 1). */
	off = ringbuf_acquire(r, n / 2 + 1);
	assert(off == 0);
	ringbuf_produce(r);

	off = ringbuf_acquire(r, n / 2 - 1);
	assert(off == -1);

	/* Consume (n / 2 + 1) bytes. */
	len = ringbuf_consume(r, &woff);
	assert(len == (n / 2 + 1) && woff == 0);
	ringbuf_release(r, len);

	/* All consumed, attempt (n / 2 + 1) now. */
	off = ringbuf_acquire(r, n / 2 + 1);
	assert(off == -1);

	/* However, wraparound can be successful with (n / 2). */
	off = ringbuf_acquire(r, n / 2);
	assert(off == 0);
	ringbuf_produce(r);

	/* Consume (n / 2) bytes. */
	len = ringbuf_consume(r, &woff);
	assert(len == (n / 2) && woff == 0);
	ringbuf_release(r, len);

	ringbuf_destroy(r);
}

static void
test_multi(void)
{
	ringbuf_t *r;
	size_t len, woff;
	ssize_t off;

	r = ringbuf_create(3);
	assert(r != NULL);
	ringbuf_register(r);

	/*
	 * Produce 2 bytes.
	 */

	off = ringbuf_acquire(r, 1);
	assert(off == 0);
	ringbuf_produce(r);

	off = ringbuf_acquire(r, 1);
	assert(off == 1);
	ringbuf_produce(r);

	off = ringbuf_acquire(r, 1);
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

	off = ringbuf_acquire(r, 2);
	assert(off == -1);

	off = ringbuf_acquire(r, 1);
	assert(off == 2);
	ringbuf_produce(r);

	off = ringbuf_acquire(r, 1);
	assert(off == 0);
	ringbuf_produce(r);

	off = ringbuf_acquire(r, 1);
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

	ringbuf_destroy(r);
}

int
main(void)
{
	test_wraparound();
	test_multi();
	puts("ok");
	return 0;
}
