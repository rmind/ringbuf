# Atomic ring buffer

Atomic multi-producer single-consumer ring buffer with passive tail update
and contiguous range operations.  This implementation is written in C11 and
distributed under the 2-clause BSD license.

## API

* `ringbuf_t *ringbuf_create(size_t length)`
  * Construct a new ring buffer of a given _length_.

* `void ringbuf_destroy(ringbuf_t *rbuf)`
  * Destroy the ring buffer object.

* `int ringbuf_register(ringbuf_t *rbuf)`
  * Register the current thread as a producer.  Each producer must register.

* `ssize_t ringbuf_acquire(ringbuf_t *rbuf, size_t len)`
  * Request a space of a given length in the ring buffer.  Returns the
  offset at which the space is available or -1 on failure.  Once the data
  is ready (typically, when writing to the ring buffer is complete), the
  `ringbuf_produce` function must be called to indicate that.

* `void ringbuf_produce(ringbuf_t *rbuf)`
  * Indicate the acquired range in the buffer is produced and is ready
  to be consumed.

* `size_t ringbuf_consume(ringbuf_t *rbuf, size_t *offset)`
  * Get a contiguous range which is ready to be consumed.  Returns zero
  if there is no data available for consumption.  Once the data is
  consumed (typically, when reading from the ring buffer is complete),
  the `ringbuf_release` function must be called to indicate that.

* `void ringbuf_release(ringbuf_t *rbuf, size_t nbytes)`
  * Indicate that the consumed range can now be released and may now be
  reused by the producers.

## Example

Producers:
```c
if (ringbuf_register(r) == -1)
	err(EXIT_FAILURE, "ringbuf_register")

...

if ((off = ringbuf_acquire(r, len)) != -1) {
	memcpy(&buf[off], payload, len);
	ringbuf_produce(r);
}
```

Consumer:
```c
if ((len = ringbuf_consume(r, &off)) != 0) {
	process(&buf[off], len);
	ringbuf_release(r, len);
}
```
