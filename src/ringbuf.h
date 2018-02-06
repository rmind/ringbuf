/*
 * Copyright (c) 2016 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef _RINGBUF_H_
#define _RINGBUF_H_

// Define signed-size-type when using MSVC.
#if defined(_MSC_VER)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;

#undef __BEGIN_DECLS
#undef __END_DECLS
#ifdef __cplusplus
# define __BEGIN_DECLS extern "C" {
# define __END_DECLS }
#else
# define __BEGIN_DECLS /* empty */
# define __END_DECLS /* empty */
#endif

#endif // _MSC_VER

__BEGIN_DECLS

typedef struct ringbuf ringbuf_t;
typedef struct ringbuf_worker ringbuf_worker_t;

int		ringbuf_setup(ringbuf_t *, unsigned, size_t);
void		ringbuf_get_sizes(unsigned, size_t *, size_t *);

ringbuf_worker_t *ringbuf_register(ringbuf_t *, unsigned);
void		ringbuf_unregister(ringbuf_t *, ringbuf_worker_t *);

ssize_t		ringbuf_acquire(ringbuf_t *, ringbuf_worker_t *, size_t);
void		ringbuf_produce(ringbuf_t *, ringbuf_worker_t *);
size_t		ringbuf_consume(ringbuf_t *, size_t *);
void		ringbuf_release(ringbuf_t *, size_t);

__END_DECLS

#endif // _RINGBUF_H_
