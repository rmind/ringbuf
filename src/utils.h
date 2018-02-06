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

#ifndef _UTILS_H_
#define _UTILS_H_

#include <assert.h>

/*
 * A regular assert (debug/diagnostic only).
 */
#if !defined(NDEBUG)
#define	ASSERT		assert
#else
#define	ASSERT(x)
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

/*
 * Branch prediction macros.
 */
#ifndef __predict_true
	#if _MSC_VER <= 1600
		#define __predict_true(x) (x)
		#define __predict_false(x) (x)
	#else //  _MSC_VER <= 1600
		#define	__predict_true(x)	__builtin_expect((x) != 0, 1)
		#define	__predict_false(x)	__builtin_expect((x) != 0, 0)
	#endif //  _MSC_VER <= 1600
#endif

/*
 * Atomic operations and memory barriers.  If C11 API is not available,
 * then wrap the GCC builtin routines.
 */
#ifndef atomic_compare_exchange_weak
	#if defined(_MSC_VER) && defined(__cplusplus) && __cplusplus < 201103L
		// Implement atomic compare-and-swap in pre-C++11 MSVC compilers.
		//
		// C++ doesn't support partial function template specialization, so
		// do it with a structure with a static method. Inspired by
		// https://stackoverflow.com/a/48218849/603828 .
		//
		// The parameters all have to be the same explicit type for this to work,
		// e.g. you'll have to cast NULL or nullptr to a type.
		template<typename T, size_t S> struct atomic_compare_exchange_weak_;
		template<typename T> struct atomic_compare_exchange_weak_<T, sizeof(char)>
			{ static inline bool call(T volatile *ptr, T expected, T desired)
				{ return (_InterlockedCompareExchange8((char volatile *)ptr, (char)desired, (char)expected) == (char)expected); } };
		template<typename T> struct atomic_compare_exchange_weak_<T, sizeof(short)>
			{ static inline bool call(T volatile *ptr, T expected, T desired)
				{ return (_InterlockedCompareExchange16((short volatile *)ptr, (short)desired, (short)expected) == (short)expected); } };
		template<typename T> struct atomic_compare_exchange_weak_<T, sizeof(long)>
			{ static inline bool call(T volatile *ptr, T expected, T desired)
				{ return (_InterlockedCompareExchange((long volatile *)ptr, (long)desired, (long)expected) == (long)expected); } };
		template<typename T> struct atomic_compare_exchange_weak_<T *, sizeof(long)>
			{ static inline bool call(T * volatile *ptr, T *expected, T *desired)
				{ return (_InterlockedCompareExchange((long volatile *)ptr, (long)desired, (long)expected) == (long)expected); } };
		template<typename T> struct atomic_compare_exchange_weak_<T, sizeof(__int64)>
			{ static inline bool call(T volatile *ptr, T expected, T desired)
				{ return (_InterlockedCompareExchange64((__int64 volatile *)ptr, (__int64)desired, (__int64)expected) == (__int64)expected); } };
		template<typename T> struct atomic_compare_exchange_weak_<T *, sizeof(__int64)>
			{ static inline bool call(T * volatile *ptr, T *expected, T *desired)
				{ return (_InterlockedCompareExchange64((__int64 volatile *)ptr, (__int64)desired, (__int64)expected) == (__int64)expected); } };
		template<typename T> inline bool atomic_compare_exchange_weak(T volatile *ptr, T expected, T desired)
			{ return atomic_compare_exchange_weak_<decltype(expected),sizeof(expected)>::call(ptr, expected, desired); }
	#elif defined(_MSC_VER) && (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L)
		// Implement the single needed atomic-compare-exchange for pre-C11 compilers.
		#define	atomic_compare_exchange_weak(ptr, expected, desired) \
			(_InterlockedCompareExchange64((volatile __int64 *)ptr, (__int64)desired, (__int64)expected) == (__int64)expected)
	#else // GCC style
		#define	atomic_compare_exchange_weak(ptr, expected, desired) \
			__sync_bool_compare_and_swap(ptr, expected, desired)
	#endif // compiler
#endif

#ifndef atomic_thread_fence
	// Define atomic-operation fences before C11.
	#if _MSC_VER <= 1600
		#define atomic_thread_fence(x) ::MemoryBarrier()
	#else //  _MSC_VER <= 1600
		#define	memory_order_acquire	__ATOMIC_ACQUIRE	// load barrier
		#define	memory_order_release	__ATOMIC_RELEASE	// store barrier
		#define	atomic_thread_fence(m)	__atomic_thread_fence(m)
	#endif //  _MSC_VER <= 1600
#endif // !atomic_thread_fence

/*
 * Exponential back-off for the spinning paths.
 */
#define	SPINLOCK_BACKOFF_MIN	4
#define	SPINLOCK_BACKOFF_MAX	128
#if defined(__x86_64__) || defined(__i386__)
#define SPINLOCK_BACKOFF_HOOK	__asm volatile("pause" ::: "memory")
#elif defined(_MSC_VER)
#define SPINLOCK_BACKOFF_HOOK	::YieldProcessor()
#else
#define SPINLOCK_BACKOFF_HOOK
#endif
#define	SPINLOCK_BACKOFF(count)					\
do {								\
	for (int __i = (count); __i != 0; __i--) {		\
		SPINLOCK_BACKOFF_HOOK;				\
	}							\
	if ((count) < SPINLOCK_BACKOFF_MAX)			\
		(count) += (count);				\
} while (/* CONSTCOND */ 0);

#endif // _UTILS_H_
