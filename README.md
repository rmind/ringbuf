# Atomic ring buffer

Atomic multi-producer single-consumer ring buffer with passive tail update
and contiguous range operations.  This implementation is written in C11 and
distributed under the 2-clause BSD license.

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
