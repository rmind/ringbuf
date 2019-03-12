# Lock-free ring buffer

[![Build Status](https://travis-ci.org/rmind/ringbuf.svg?branch=master)](https://travis-ci.org/rmind/ringbuf)

Lock-free multi-producer single-consumer (MPSC) ring buffer which supports
contiguous range operations and which can be conveniently used for message
passing.  The implementation is written in C11 and distributed under the
2-clause BSD license.

## API

* `int ringbuf_setup(ringbuf_t *rbuf, unsigned nworkers, size_t length)`
  * Setup a new ring buffer of a given _length_.  The `rbuf` is a pointer
  to the opaque ring buffer object; the caller is responsible to allocate
  the space for this object.  Typically, the object would be allocated
  dynamically if using threads or reserved in a shared memory blocked if
  using processes.  The allocation size for the object shall be obtained
  using the `ringbuf_get_sizes` function.  Returns 0 on success and -1
  on failure.

* `void ringbuf_get_sizes(unsigned nworkers, size_t *ringbuf_obj_size, size_t *ringbuf_worker_size)`
  * Returns the size of the opaque `ringbuf_t` and, optionally, `ringbuf_worker_t` structures.
  The size of the `ringbuf_t` structure depends on the number of workers,
  specified by the `nworkers` parameter.

* `ringbuf_worker_t *ringbuf_register(ringbuf_t *rbuf, unsigned i)`
  * Register the current worker (thread or process) as a producer.  Each
  producer MUST register itself.  The `i` is a worker number, starting
  from zero (i.e. shall be than `nworkers` used in the setup).  On success,
  returns a pointer to an opaque `ringbuf_worker_t` structured, which is
  a part of the `ringbuf_t` memory block.  On failure, returns `NULL`.

* `void ringbuf_unregister(ringbuf_t *rbuf, ringbuf_worker_t *worker)`
  * Unregister the specified worker from the list of producers.

* `ssize_t ringbuf_acquire(ringbuf_t *rbuf, ringbuf_worker_t *worker, size_t len)`
  * Request a space of a given length in the ring buffer.  Returns the
  offset at which the space is available or -1 on failure.  Once the data
  is ready (typically, when writing to the ring buffer is complete), the
  `ringbuf_produce` function must be called to indicate that.  Nested
  acquire calls are not allowed.

* `void ringbuf_produce(ringbuf_t *rbuf, ringbuf_worker_t *worker)`
  * Indicate that the acquired range in the buffer is produced and is ready
  to be consumed.

* `size_t ringbuf_consume(ringbuf_t *rbuf, size_t *offset)`
  * Get a contiguous range which is ready to be consumed.  Returns zero
  if there is no data available for consumption.  Once the data is
  consumed (typically, when reading from the ring buffer is complete),
  the `ringbuf_release` function must be called to indicate that.

* `void ringbuf_release(ringbuf_t *rbuf, size_t nbytes)`
  * Indicate that the consumed range can now be released and may now be
  reused by the producers.

## Notes

The consumer will return a contiguous block of ranges produced i.e. the
`ringbuf_consume` call will not return partial ranges.  If you think of
produced range as a message, then consumer will return a block of messages,
always ending at the message boundary.  Such behaviour allows us to use
this ring buffer implementation as a message queue.

The implementation was extensively tested on a 24-core x86 machine,
see [the stress test](src/t_stress.c) for the details on the technique.
It also provides an example how the mechanism can be used for message
passing.

## Caveats

This ring buffer implementation always provides a contiguous range of
space for the producer.  It is achieved by an early wrap-around if the
requested range cannot fit in the end.  The implication of this is that
the `ringbuf_acquire` call may fail if the requested range is greater
than half of the buffer size.  Hence, it may be necessary to ensure that
the ring buffer size is at least twice as large as the maximum production
unit size.

It should also be noted that one of the trade-offs of such design is that
the consumer currently performs an O(n) scan on the list of producers.

## Example

Producers:
```c
if ((w = ringbuf_register(r, worker_id)) == NULL)
	err(EXIT_FAILURE, "ringbuf_register")

...

if ((off = ringbuf_acquire(r, w, len)) != -1) {
	memcpy(&buf[off], payload, len);
	ringbuf_produce(r, tls);
}
```

Consumer:
```c
if ((len = ringbuf_consume(r, &off)) != 0) {
	process(&buf[off], len);
	ringbuf_release(r, len);
}
```
