/*
 * Copyright (c) 2016 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * Atomic multi-producer single-consumer ring buffer with passive
 * tail update and contiguous range operations.
 *
 * There are three offsets -- think of clock hands:
 * - NEXT: marks the beginning of the available space,
 * - WRITTEN: the point up to which the data is actually written.
 * - Observed READY: point up to which data is ready to be written.
 *
 * Producers
 *
 *	Observe and save the 'next' offset, then request N bytes from
 *	the ring buffer by atomically advancing the 'next' offset.  Once
 *	the data is written into the "reserved" buffer space, the thread
 *	clears the saved value; these observed values are used to compute
 *	the 'ready' offset.
 *
 * Consumer
 *
 *	Writes the data between 'written' and 'ready' offsets and updates
 *	the 'written' value.  The consumer thread thread scans for the
 *	lowest seen value by the producers.
 *
 * Key invariant
 *
 *	Producers cannot go beyond the 'written' offset; producers are
 *	also not allowed to catch up with the consumer.  Only the consumer
 *	is allowed to catch up with the producer i.e. set the 'written'
 *	offset to be equal to the 'next' offset.
 *
 * Wrap-around
 *
 *	If the producer cannot acquire the requested length due to little
 *	available space at the end of the buffer, then it will wraparound.
 *	WRAP_LOCK_BIT in 'next' offset is used to lock the 'end' offset.
 *
 *	There is an ABA problem if one producer stalls while a pair of
 *	producer and consumer would both successfully wrap-around and set
 *	the 'next' offset to the stale value of the first producer, thus
 *	letting it to perform a successful CAS violating the invariant.
 *	A counter in the 'next' offset (masked by WRAP_COUNTER) is used
 *	to prevent from this problem.  It is incremented on wraparounds.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#include "ringbuf.h"
#include "utils.h"

#define	RBUF_OFF_MAX	(UINT64_MAX)
#define	RBUF_OFF_MASK	(0x00000000ffffffffUL)
#define	WRAP_LOCK_BIT	(0x8000000000000000UL)

#define	WRAP_COUNTER	(0x7fffffff00000000UL)
#define	WRAP_INCR(x)	(((x) + 0x100000000UL) & WRAP_COUNTER)

typedef uint64_t	ringbuf_off_t;

typedef struct ringbuf_tls {
	ringbuf_off_t	seen_off;
	struct ringbuf_tls *next;
} ringbuf_tls_t;

struct ringbuf {
	/* Ring buffer space and TLS key. */
	size_t		space;
	pthread_key_t	tls_key;

	/*
	 * The NEXT hand is atomically updated by the producer.
	 * WRAP_LOCK_BIT is set in case of wrap-around; in such case,
	 * the producer can update the 'end' offset.
	 */
	ringbuf_off_t	next;
	ringbuf_off_t	end;

	/* The following are updated by the consumer. */
	ringbuf_off_t	written;
	ringbuf_tls_t *	list;
};

/*
 * ringbuf_create: construct a new ring buffer of a given length.
 */
ringbuf_t *
ringbuf_create(size_t length)
{
	ringbuf_t *rbuf;

	if (length >= RBUF_OFF_MASK) {
		errno = EINVAL;
		return NULL;
	}
	rbuf = calloc(1, sizeof(ringbuf_t));
	if (!rbuf) {
		return NULL;
	}
	if (pthread_key_create(&rbuf->tls_key, free) != 0) {
		free(rbuf);
		return NULL;
	}
	rbuf->space = length;
	rbuf->end = RBUF_OFF_MAX;
	return rbuf;
}

/*
 * ringbuf_register: register the current thread as a producer.
 */
int
ringbuf_register(ringbuf_t *rbuf)
{
	ringbuf_tls_t *t, *head;

	t = pthread_getspecific(rbuf->tls_key);
	if (__predict_false(t == NULL)) {
		if ((t = malloc(sizeof(ringbuf_tls_t))) == NULL) {
			return -1;
		}
		pthread_setspecific(rbuf->tls_key, t);
	}
	memset(t, 0, sizeof(ringbuf_tls_t));
	t->seen_off = RBUF_OFF_MAX;

	do {
		head = rbuf->list;
		t->next = head;
	} while (!atomic_compare_exchange_weak(&rbuf->list, head, t));

	return 0;
}

/*
 * ringbuf_destroy: destroy the ring buffer object.
 */
void
ringbuf_destroy(ringbuf_t *rbuf)
{
	pthread_key_delete(rbuf->tls_key);
	free(rbuf);
}
/*
 * stable_nextoff: capture and return a stable value of the 'next' offset.
 */
static inline ringbuf_off_t
stable_nextoff(ringbuf_t *rbuf)
{
	unsigned count = SPINLOCK_BACKOFF_MIN;
	ringbuf_off_t next;

	while ((next = rbuf->next) & WRAP_LOCK_BIT) {
		SPINLOCK_BACKOFF(count);
	}
	atomic_thread_fence(memory_order_acquire);
	ASSERT((next & RBUF_OFF_MASK) < rbuf->space);
	return next;
}

/*
 * ringbuf_acquire: request a space of a given length in the ring buffer.
 *
 * => On success: returns the offset at which the space is available.
 * => On failure: returns -1.
 */
ssize_t
ringbuf_acquire(ringbuf_t *rbuf, size_t len)
{
	ringbuf_off_t seen, next, target;
	ringbuf_tls_t *t;

	ASSERT(len > 0 && len < rbuf->space);
	t = pthread_getspecific(rbuf->tls_key);
	ASSERT(t != NULL);
	ASSERT(t->seen_off == RBUF_OFF_MAX);

	do {
		ringbuf_off_t written;

		/*
		 * Get the stable 'next' offset.  Save the observed 'next'
		 * value.  Note: CAS will issue a memory_order_release for
		 * us and thus ensures that it reaches global visibility
		 * together with new 'next'.
		 */
		seen = stable_nextoff(rbuf);
		next = seen & RBUF_OFF_MASK;
		ASSERT(next < rbuf->space);
		t->seen_off = next;

		/*
		 * Compute the target offset.  Key invariant: we cannot
		 * go beyond the WRITTEN offset or catch up with it.
		 */
		target = next + len;
		written = rbuf->written;
		if (__predict_false(next < written && target >= written)) {
			/* The producer must wait. */
			t->seen_off = RBUF_OFF_MAX;
			return -1;
		}

		if (__predict_false(target >= rbuf->space)) {
			const bool exceed = target > rbuf->space;

			/*
			 * Wrap-around and start from the beginning.
			 *
			 * If we would exceed the buffer, the attempt to
			 * acquire the WRAP_LOCK_BIT and use the space in
			 * the beginning.  If we used all space exactly to
			 * the end, then reset to 0.
			 *
			 * Check the invariant again.
			 */
			target = exceed ? (WRAP_LOCK_BIT | len) : 0;
			if ((target & RBUF_OFF_MASK) >= written) {
				t->seen_off = RBUF_OFF_MAX;
				return -1;
			}
			/* Increment the wrap-around counter. */
			target |= WRAP_INCR(seen & WRAP_COUNTER);
		} else {
			/* Preserve the wrap-around counter. */
			target |= seen & WRAP_COUNTER;
		}

	} while (!atomic_compare_exchange_weak(&rbuf->next, seen, target));

	/*
	 * Acquired the range.  If are performing the wrap-around
	 * i.e. set WRAP_LOCK_BIT, then set the 'end' offset.
	 */
	if (__predict_false(target & WRAP_LOCK_BIT)) {
		/* Cannot wrap-around again if consumer did not catch-up. */
		ASSERT(rbuf->written <= next);
		ASSERT(rbuf->end == RBUF_OFF_MAX);
		rbuf->end = next;

		/*
		 * Unlock: ensure the 'end' offset reaches global
		 * visibility before the lock is released.
		 */
		atomic_thread_fence(memory_order_release);
		rbuf->next = (target & ~WRAP_LOCK_BIT);
	}
	ASSERT((target & RBUF_OFF_MASK) <= rbuf->space);
	return (ssize_t)next;
}

/*
 * ringbuf_produce: indicate the acquired range in the buffer is produced
 * and is ready to be consumed.
 */
void
ringbuf_produce(ringbuf_t *rbuf)
{
	ringbuf_tls_t *t;

	t = pthread_getspecific(rbuf->tls_key);
	ASSERT(t != NULL);
	ASSERT(t->seen_off != RBUF_OFF_MAX);

	atomic_thread_fence(memory_order_release);
	t->seen_off = RBUF_OFF_MAX;
}

/*
 * ringbuf_consume: get a contiguous range which is ready to be consumed.
 */
size_t
ringbuf_consume(ringbuf_t *rbuf, size_t *offset)
{
	ringbuf_off_t written = rbuf->written, next, ready;
	ringbuf_tls_t *t;
	size_t towrite;

	/*
	 * Get the stable 'next' offset.  Did producers wrap-around?
	 */
	next = stable_nextoff(rbuf) & RBUF_OFF_MASK;
	if (next < written) {
		ringbuf_off_t end;

		/*
		 * Yes: we must consume the end of the buffer first.
		 * The producer might have set the 'end' point (note
		 * that we spin-wait for WRAP_LOCK_BIT).
		 */
		end = MIN(rbuf->space, rbuf->end);
		rbuf->end = RBUF_OFF_MAX;
		atomic_thread_fence(memory_order_release);

		if (end > written) {
			*offset = written;
			return end - written;
		}

		/* Wrap-around the consumer. */
		rbuf->written = written = 0;
	}

	/*
	 * The area between the 'written' offset and the 'next' offset
	 * is the *preliminary* target buffer area to be consumed.
	 */
	ready = next;

	/*
	 * Determine the 'ready' offset.  Note: at this point, some
	 * producer might have already triggered the wrap around,
	 * therefore we have to filter the seen values.
	 */
	t = rbuf->list;
	while (t) {
		ringbuf_off_t seen_off = t->seen_off;

		if (seen_off > written) {
			ready = MIN(seen_off, ready);
		}
		ASSERT(ready >= written);
		t = t->next;
	}
	towrite = ready - written;
	*offset = written;
	ASSERT(towrite <= rbuf->space);
	return towrite;
}

/*
 * ringbuf_release: indicate that the consumed range can now be released.
 */
void
ringbuf_release(ringbuf_t *rbuf, size_t nbytes)
{
	const size_t nwritten = rbuf->written + nbytes;
	ASSERT(rbuf->written <= rbuf->space);
	rbuf->written = (nwritten == rbuf->space) ? 0 : nwritten;
}
