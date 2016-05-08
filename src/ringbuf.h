/*
 * Copyright (c) 2016 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef _RINGBUF_H_
#define _RINGBUF_H_

#include <stdbool.h>

__BEGIN_DECLS

typedef struct ringbuf ringbuf_t;

ringbuf_t *	ringbuf_create(size_t);
void		ringbuf_destroy(ringbuf_t *);
int		ringbuf_register(ringbuf_t *);

ssize_t		ringbuf_acquire(ringbuf_t *, size_t);
void		ringbuf_produce(ringbuf_t *);
size_t		ringbuf_consume(ringbuf_t *, size_t *);
void		ringbuf_release(ringbuf_t *, size_t);

__END_DECLS

#endif
