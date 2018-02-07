/*
 * Copyright (c) 2016-2017 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * Atomic multi-producer single-consumer ring buffer, which supports
 * contiguous range operations and which can be conveniently used for
 * message passing.
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
 *	the 'written' value.  The consumer thread scans for the lowest
 *	seen value by the producers.
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
 *
 *	The same ABA problem could also cause a stale 'ready' offset,
 *	which could be observed by the consumer.  We set WRAP_LOCK_BIT in
 *	the 'seen' value before advancing the 'next' and clear this bit
 *	after the successful advancing; this ensures that only the stable
 *	'ready' observed by the consumer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include "ringbuf.h"
#include "utils.h"

#define	RBUF_OFF_MASK	(0x00000000ffffffffUL)
#define	WRAP_LOCK_BIT	(0x8000000000000000UL)
#define	RBUF_OFF_MAX	(UINT64_MAX & ~WRAP_LOCK_BIT)

#define	WRAP_COUNTER	(0x7fffffff00000000UL)
#define	WRAP_INCR(x)	(((x) + 0x100000000UL) & WRAP_COUNTER)

#define	WORKER_NULL		(0x00000000ffffffffUL)

typedef uint64_t	ringbuf_off_t;
typedef uint64_t	worker_off_t;

struct ringbuf_worker {
	volatile ringbuf_off_t	seen_off;
	volatile worker_off_t	next;
};

struct ringbuf {
	/* Ring buffer space. */
	size_t			space;

	/*
	 * The NEXT hand is atomically updated by the producer.
	 * WRAP_LOCK_BIT is set in case of wrap-around; in such case,
	 * the producer can update the 'end' offset.
	 */
	volatile ringbuf_off_t	next;
	ringbuf_off_t		end;

	/* Track acquires that haven't finished producing yet. */
	worker_off_t		used_workers, free_workers;

	/* The following are updated by the consumer. */
	ringbuf_off_t		written;
	unsigned			nworkers;
	ringbuf_worker_t	workers[];
};

/*
 * ringbuf_setup: initialise a new ring buffer of a given length.
 */
int
ringbuf_setup(ringbuf_t *rbuf, unsigned nworkers, size_t length)
{
	if (length >= RBUF_OFF_MASK) {
		errno = EINVAL;
		return -1;
	}
	memset(rbuf, 0, offsetof(ringbuf_t, workers[nworkers]));
	rbuf->space = length;
	rbuf->end = RBUF_OFF_MAX;
	rbuf->nworkers = nworkers;

	/* Put all workers into the free-stack. */
	rbuf->used_workers = rbuf->free_workers = WORKER_NULL;
	for (unsigned i = 0; i < rbuf->nworkers; i++) {
		rbuf->workers[i].seen_off = RBUF_OFF_MAX;
		rbuf->workers[i].next = rbuf->free_workers;
		rbuf->free_workers = i;
	}
	return 0;
}

/*
 * ringbuf_get_sizes: return the sizes of the ringbuf_t and ringbuf_worker_t.
 */
void
ringbuf_get_sizes(unsigned nworkers,
    size_t *ringbuf_size, size_t *ringbuf_worker_size)
{
	if (ringbuf_size)
		*ringbuf_size = offsetof(ringbuf_t, workers[nworkers]);
	if (ringbuf_worker_size)
		*ringbuf_worker_size = sizeof(ringbuf_worker_t);
}

/*
 * ringbuf_register: register the worker (thread/process) as a producer
 * and pass the pointer to its local store.
 */
ringbuf_worker_t *
ringbuf_register(ringbuf_t *rbuf, unsigned i)
{
	/* Deprecated. */
	(void)rbuf;
	(void)i;
	return NULL;
}

void
ringbuf_unregister(ringbuf_t *rbuf, ringbuf_worker_t *w)
{
	/* Deprecated. */
	(void)rbuf;
	(void)w;
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
 * push_worker: push this worker-record onto the given stack.
 */
static inline void
push_worker(ringbuf_t *rbuf, worker_off_t volatile *stack_head,
	ringbuf_worker_t *w)
{
	worker_off_t w_offset, old_head, new_head;

	/* Get the offset of the worker-record being pushed. */
	w_offset = w - rbuf->workers;

	/* Make sure this worker-record isn't on any stack already. */
	ASSERT(w->next == WORKER_NULL);

	do {
		/* Get the offset of the next worker-record on the stack. */
		old_head = *stack_head;

		/*
		 * Prepare to push that worker-record onto the stack,
		 * i.e. increment the version-number of the stack-head index.
		 *
		 * Since this worker-record isn't on any stack at this point,
		 * nothing about its next-offset (including its version-number)
		 * has to be preserved.
		 */
		w->next = (old_head & RBUF_OFF_MASK);
		new_head = (w_offset | WRAP_INCR(old_head));
	} while (!atomic_compare_exchange_weak(stack_head, old_head, new_head));
}

/*
 * pop_worker: pop a worker-record from the given stack.
 */
static inline ringbuf_worker_t *
pop_worker(ringbuf_t *rbuf, worker_off_t volatile *stack_head)
{
	worker_off_t old_head, new_head;
	ringbuf_worker_t *w;

	do {
		worker_off_t old_head_offset;

		/* Get the offset of the worker-record on top of the stack. */
		old_head = *stack_head;
		old_head_offset = (old_head & RBUF_OFF_MASK);
		if (old_head_offset == WORKER_NULL)
			return NULL;

		/* Find that worker-record. */
		w = &rbuf->workers[old_head_offset];

		/*
		 * Prepare to pop that worker-record off of the stack,
		 * i.e. increment the version-number of the stack-head index.
		 */
		new_head = (w->next & RBUF_OFF_MASK);
		new_head |= WRAP_INCR(old_head);
	} while (!atomic_compare_exchange_weak(stack_head, old_head, new_head));

	/*
	 * Since this worker-record isn't on any stack at this point,
	 * nothing about its next-offset (including its version-number)
	 * has to be preserved.
	 */
	w->next = WORKER_NULL;

	return w;
}

/*
 * try_unlink_worker: try to unlink a worker-record from a stack.
 */
static inline bool
try_unlink_worker(ringbuf_t *rbuf, worker_off_t volatile *stack_link,
	worker_off_t old_link)
{
	ringbuf_worker_t *w;
	worker_off_t old_link_offset, new_link;
	bool success;

	/* Find that worker-record. */
	old_link_offset = (old_link & RBUF_OFF_MASK);
	ASSERT (old_link_offset != WORKER_NULL);
	w = &rbuf->workers[old_link_offset];

	/*
	 * Prepare to unlink that worker-record from the stack,
	 * i.e. increment the version-number of the stack-link index.
	 */
	new_link = (w->next & RBUF_OFF_MASK);
	new_link |= WRAP_INCR(old_link);
	success = atomic_compare_exchange_weak(stack_link, old_link, new_link);

	/*
	 * Since this worker-record isn't on any stack at this point,
	 * nothing about its next-offset (including its version-number)
	 * has to be preserved.
	 */
	if (success)
		w->next = WORKER_NULL;

	return success;
}

/*
 * ringbuf_acquire: request a space of a given length in the ring buffer.
 *
 * => On success: returns the offset at which the space is available.
 * => On failure: returns -1.
 */
ssize_t
ringbuf_acquire(ringbuf_t *rbuf, ringbuf_worker_t **pw, size_t len)
{
	ringbuf_off_t seen, next, target;
	ringbuf_worker_t *w;
	ringbuf_off_t seen_off;

	ASSERT(len > 0 && len <= rbuf->space);

	/* Get a worker-record, to track state between acquire & produce. */
	*pw = NULL;
	w = pop_worker(rbuf, &rbuf->free_workers);
	if (w == NULL)
		return -1;
	ASSERT(w->seen_off == RBUF_OFF_MAX);

	do {
		ringbuf_off_t written;

		/*
		 * Get the stable 'next' offset.  Save the observed 'next'
		 * value (i.e. the 'seen' offset), but mark the value as
		 * unstable (set WRAP_LOCK_BIT).
		 *
		 * Note: CAS will issue a memory_order_release for us and
		 * thus ensures that it reaches global visibility together
		 * with new 'next'.
		 */
		seen = stable_nextoff(rbuf);
		next = seen & RBUF_OFF_MASK;
		ASSERT(next < rbuf->space);
		seen_off = next | WRAP_LOCK_BIT;

		/*
		 * Compute the target offset.  Key invariant: we cannot
		 * go beyond the WRITTEN offset or catch up with it.
		 */
		target = next + len;
		written = rbuf->written;
		if (__predict_false(next < written && target >= written)) {
			/* Free this unused worker-record. */
			push_worker(rbuf, &rbuf->free_workers, w);

			/* The producer must wait. */
			return -1;
		}

		if (__predict_false(target >= rbuf->space)) {
			const bool exceed = target > rbuf->space;

			/*
			 * Wrap-around and start from the beginning.
			 *
			 * If we would exceed the buffer, then attempt to
			 * acquire the WRAP_LOCK_BIT and use the space in
			 * the beginning.  If we used all space exactly to
			 * the end, then reset to 0.
			 *
			 * Check the invariant again.
			 */
			target = exceed ? (WRAP_LOCK_BIT | len) : 0;
			if ((target & RBUF_OFF_MASK) >= written) {
				/* Free this unused worker-record. */
				push_worker(rbuf, &rbuf->free_workers, w);

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
	 * Acquired the range.  Clear WRAP_LOCK_BIT in the 'seen' value
	 * thus indicating that it is stable now.
	 */
	w->seen_off = (seen_off & ~WRAP_LOCK_BIT);
	push_worker(rbuf, &rbuf->used_workers, w);

	/* Hand this worker-record back to our caller. */
	*pw = w;

	/*
	 * If we set the WRAP_LOCK_BIT in the 'next' (because we exceed
	 * the remaining space and need to wrap-around), then save the
	 * 'end' offset and release the lock.
	 */
	if (__predict_false(target & WRAP_LOCK_BIT)) {
		/* Cannot wrap-around again if consumer did not catch-up. */
		ASSERT(rbuf->written <= next);
		ASSERT(rbuf->end == RBUF_OFF_MAX);
		rbuf->end = next;
		next = 0;

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
ringbuf_produce(ringbuf_t *rbuf, ringbuf_worker_t *w)
{
	(void)rbuf;
	ASSERT(w->seen_off != RBUF_OFF_MAX);
	atomic_thread_fence(memory_order_release);
	w->seen_off = RBUF_OFF_MAX;
}

/*
 * ringbuf_consume: get a contiguous range which is ready to be consumed.
 */
size_t
ringbuf_consume(ringbuf_t *rbuf, size_t *offset)
{
	ringbuf_off_t written = rbuf->written, next, ready;
	worker_off_t volatile *pw_link;
	worker_off_t w_link, w_off;
	size_t towrite;
retry:
	/*
	 * Get the stable 'next' offset.  Note: stable_nextoff() issued
	 * a load memory barrier.  The area between the 'written' offset
	 * and the 'next' offset will be the *preliminary* target buffer
	 * area to be consumed.
	 */
	next = stable_nextoff(rbuf) & RBUF_OFF_MASK;
	if (written == next) {
		/* If producers did not advance, then nothing to do. */
		return 0;
	}

	/*
	 * Observe the 'ready' offset of each producer.
	 *
	 * At this point, some producer might have already triggered the
	 * wrap-around and some (or all) seen 'ready' values might be in
	 * the range between 0 and 'written'.  We have to skip them.
	 */
	ready = RBUF_OFF_MAX;

	pw_link = &rbuf->used_workers;
	w_link = *pw_link;
	w_off = (w_link & RBUF_OFF_MASK);
	while (w_off != WORKER_NULL) {
		ringbuf_worker_t *w = &rbuf->workers[w_off];
		unsigned count = SPINLOCK_BACKOFF_MIN;
		ringbuf_off_t seen_off;

		/*
		 * Get a stable 'seen' value.  This is necessary since we
		 * want to discard the stale 'seen' values.
		 */
		while ((seen_off = w->seen_off) & WRAP_LOCK_BIT) {
			SPINLOCK_BACKOFF(count);
		}

		/* If this worker has produced, clean it up. */
		if (seen_off == RBUF_OFF_MAX) {
			/*
			 * Try to unlink this worker-record from the used-worker
			 * stack.
			 * If it can't be done, try again later.
			 */
			if (try_unlink_worker(rbuf, pw_link, w_link)) {
				/* Free this unused worker-record. */
				w->seen_off = RBUF_OFF_MAX;
				push_worker(rbuf, &rbuf->free_workers, w);
				w_link = *pw_link;
				w_off = (w_link & RBUF_OFF_MASK);
				continue;
			}
		}

		/*
		 * Ignore the offsets after the possible wrap-around.
		 * We are interested in the smallest seen offset that is
		 * not behind the 'written' offset.
		 */
		if (seen_off >= written) {
			ready = MIN(seen_off, ready);
		}
		ASSERT(ready >= written);

		/* Move to the next incomplete acquire/produce operation. */
		pw_link = &w->next;
		w_link = *pw_link;
		w_off = (w_link & RBUF_OFF_MASK);
	}

	/*
	 * Finally, we need to determine whether wrap-around occurred
	 * and deduct the safe 'ready' offset.
	 */
	if (next < written) {
		const ringbuf_off_t end = MIN(rbuf->space, rbuf->end);

		/*
		 * Wrap-around case.  Check for the cut off first.
		 *
		 * Reset the 'written' offset if it reached the end of
		 * the buffer or the 'end' offset (if set by a producer).
		 * However, we must check that the producer is actually
		 * done (the observed 'ready' offsets are clear).
		 */
		if (ready == RBUF_OFF_MAX && written == end) {
			/*
			 * Clear the 'end' offset if was set.
			 */
			if (rbuf->end != RBUF_OFF_MAX) {
				rbuf->end = RBUF_OFF_MAX;
				atomic_thread_fence(memory_order_release);
			}
			/* Wrap-around the consumer and start from zero. */
			rbuf->written = written = 0;
			goto retry;
		}

		/*
		 * We cannot wrap-around yet; there is data to consume at
		 * the end.  The ready range is smallest of the observed
		 * 'ready' or the 'end' offset.  If neither is set, then
		 * the actual end of the buffer.
		 */
		ASSERT(ready > next);
		ready = MIN(ready, end);
		ASSERT(ready >= written);
	} else {
		/*
		 * Regular case.  Up to the observed 'ready' (if set)
		 * or the 'next' offset.
		 */
		ready = MIN(ready, next);
	}
	towrite = ready - written;
	*offset = written;

	ASSERT(ready >= written);
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
	ASSERT(rbuf->written <= rbuf->end);
	ASSERT(nwritten <= rbuf->space);

	rbuf->written = (nwritten == rbuf->space) ? 0 : nwritten;
}
