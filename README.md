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
  * Register the current thread as a producer.

* `ssize_t ringbuf_acquire(ringbuf_t *rbuf, size_t len)`
  * Request a space of a given length in the ring buffer.  On success,
  returns the offset at which the space is available; and -1 on failure.

* `size_t ringbuf_consume(ringbuf_t *rbuf, size_t *offset)`
  * Get a contiguous range which is ready to be consumed.

* `void ringbuf_produce(ringbuf_t *rbuf)`
  * Indicate the acquired range in the buffer is produced and is ready
  to be consumed.

* `void ringbuf_release(ringbuf_t *rbuf, size_t nbytes)`
  * Indicate that the consumed range can now be released.

## Example

Producers:
```c
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
