/*
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Berkeley Software Design, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)cdefs.h	8.8 (Berkeley) 1/9/95
 */

/*
 * Various helper macros.  Portions are taken from NetBSD's sys/cdefs.h
 * and sys/param.h headers.
 */

#ifndef	_UTILS_H_
#define	_UTILS_H_

#include <stdbool.h>
#include <inttypes.h>
#include <limits.h>
#include <assert.h>

#ifndef __predict_true
#define	__predict_true(x)	__builtin_expect((x) != 0, 1)
#define	__predict_false(x)	__builtin_expect((x) != 0, 0)
#endif

#ifndef __constructor
#define	__constructor		__attribute__((constructor))
#endif

#ifndef __packed
#define	__packed		__attribute__((__packed__))
#endif

#ifndef __aligned
#define	__aligned(x)		__attribute__((__aligned__(x)))
#endif

#ifndef __unused
#define	__unused		__attribute__((__unused__))
#endif

#ifndef __arraycount
#define	__arraycount(__x)	(sizeof(__x) / sizeof(__x[0]))
#endif

#ifndef __UNCONST
#define	__UNCONST(a)		((void *)(unsigned long)(const void *)(a))
#endif

#ifndef container_of
#define	container_of(PTR, TYPE, FIELD)					\
    ((TYPE *)(((char *)(PTR)) - offsetof(TYPE, FIELD)))
#endif

/*
 * Minimum, maximum and rounding macros.
 */

#ifndef MIN
#define	MIN(x, y)	((x) < (y) ? (x) : (y))
#endif

#ifndef MAX
#define	MAX(x, y)	((x) > (y) ? (x) : (y))
#endif

#ifndef roundup
#define	roundup(x, y)	((((x)+((y)-1))/(y))*(y))
#endif

#ifndef rounddown
#define	rounddown(x,y)	(((x)/(y))*(y))
#endif

#ifndef roundup2
#define	roundup2(x, m)	(((x) + (m) - 1) & ~((m) - 1))
#endif

/*
 * Helpers to fetch bytes into 32-bit or 64-bit unsigned integer.
 * On x86/amd64 -- just perform unaligned word fetch.
 */
#if defined(__x86_64__) || defined(__i386__)
#define BYTE_FETCH32(x)	htonl(*(uint32_t *)(x))
#else
#define BYTE_FETCH32(x) \
    ((uint32_t)((x)[0]) << 24 | (uint32_t)((x)[1]) << 16 | \
     (uint32_t)((x)[2]) <<  8 | (uint32_t)((x)[3]))
#endif
#define BYTE_FETCH64(x) \
    ((uint64_t)BYTE_FETCH32(x) << 32 | BYTE_FETCH32(x + 4))

/*
 * Maths helpers: log2 on integer, fast division and remainder.
 */

#ifndef flsl
static inline int
flsl(unsigned long x)
{
	return __predict_true(x) ?
	    (sizeof(unsigned long) * CHAR_BIT) - __builtin_clzl(x) : 0;
}
#define	flsl(x)		flsl(x)
#endif
#ifndef flsll
static inline int
flsll(unsigned long long x)
{
	return __predict_true(x) ?
	    (sizeof(unsigned long long) * CHAR_BIT) - __builtin_clzll(x) : 0;
}
#define	flsll(x)	flsl(x)
#endif

#ifndef ilog2
#define	ilog2(x)	(flsl(x) - 1)
#endif

/*
 * A regular assert (debug/diagnostic only).
 */
#if !defined(ASSERT)
#define	ASSERT		assert
#endif

/*
 * Compile-time assertion: if C11 static_assert() is not available,
 * then emulate it.
 */
#ifndef static_assert
#ifndef CTASSERT
#define	CTASSERT(x, y)		__CTASSERT99(x, __INCLUDE_LEVEL__, __LINE__)
#define	__CTASSERT99(x, a, b)	__CTASSERT0(x, __CONCAT(__ctassert,a), \
					       __CONCAT(_,b))
#define	__CTASSERT0(x, y, z)	__CTASSERT1(x, y, z)
#define	__CTASSERT1(x, y, z)	typedef char y ## z[(x) ? 1 : -1] __unused
#endif
#define	static_assert(exp, msg)	CTASSERT(exp)
#endif

/*
 * Atomic operations and memory barriers.  If C11 API is not available,
 * then wrap the GCC builtin routines.
 */

#ifndef atomic_compare_exchange_weak
#define	atomic_compare_exchange_weak(ptr, expected, desired) \
    __sync_bool_compare_and_swap(ptr, expected, desired)
#endif
#ifndef atomic_exchange
static inline void *
atomic_exchange(volatile void *ptr, void *nptr)
{
	volatile void * volatile old;

	do {
		old = *(volatile void * volatile *)ptr;
	} while (!atomic_compare_exchange_weak(
	    (volatile void * volatile *)ptr, old, nptr));

	return (void *)(uintptr_t)old; // workaround for gcc warnings
}
#endif
#ifndef atomic_fetch_add
#define	atomic_fetch_add(x,a)	__sync_fetch_and_add(x, a)
#endif

#ifndef atomic_thread_fence
/*
 * memory_order_acquire	- membar_consumer/smp_rmb
 * memory_order_release	- membar_producer/smp_wmb
 */
#define	memory_order_acquire	__atomic_thread_fence(__ATOMIC_ACQUIRE)
#define	memory_order_release	__atomic_thread_fence(__ATOMIC_RELEASE)
#define	atomic_thread_fence(m)	m
#endif

/*
 * Exponential back-off for the spinning paths.
 */
#define	SPINLOCK_BACKOFF_MIN	4
#define	SPINLOCK_BACKOFF_MAX	128
#if defined(__x86_64__) || defined(__i386__)
#define SPINLOCK_BACKOFF_HOOK	__asm volatile("pause" ::: "memory")
#else
#define SPINLOCK_BACKOFF_HOOK
#endif
#define	SPINLOCK_BACKOFF(count)					\
do {								\
	int __i;						\
	for (__i = (count); __i != 0; __i--) {			\
		SPINLOCK_BACKOFF_HOOK;				\
	}							\
	if ((count) < SPINLOCK_BACKOFF_MAX)			\
		(count) += (count);				\
} while (/* CONSTCOND */ 0);

/*
 * Cache line size - a reasonable upper bound.
 */
#define	CACHE_LINE_SIZE		64

#endif
