#pragma once
#ifndef SPSC_RING_HPP
#define SPSC_RING_HPP

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <cstring>
#include <type_traits>
#include <algorithm>

// Simple single-producer single-consumer ring buffer (lock-free).
// - Capacity is rounded up to the next power of two.
// - Byte-oriented when T=uint8_t. Works for POD T generally.
// API:
//   SpscRing<uint8_t> rb(cap);
//   size_t w = rb.write(src, n);   // producer thread
//   size_t r = rb.read(dst, n);    // consumer thread
//   size_t readable() const;
//   size_t writable() const;

template <typename T>
class SpscRing {
    static_assert(std::is_trivial<T>::value, "SpscRing requires trivial T");
public:
    explicit SpscRing(size_t capacity_pow2)
        : _cap(pow2ceil(capacity_pow2)),
        _mask(_cap - 1),
        _buf(new T[_cap]),
        _head(0),
        _tail(0) {}

    ~SpscRing() { delete[] _buf; }

    size_t capacity() const { return _cap; }

    size_t readable() const {
        size_t h = _head.load(std::memory_order_acquire);
        size_t t = _tail.load(std::memory_order_acquire);
        return h - t;
    }

    size_t writable() const {
        return _cap - 1 - readable();
    }

    size_t write(const T* src, size_t n) {
        size_t can = writable();
        if (n > can) n = can;
        size_t h = _head.load(std::memory_order_relaxed);
        size_t first = std::min(n, _cap - (h & _mask));
        std::memcpy(_buf + (h & _mask), src, first * sizeof(T));
        if (n > first) {
            std::memcpy(_buf, src + first, (n - first) * sizeof(T));
        }
        _head.store(h + n, std::memory_order_release);
        return n;
    }

    size_t read(T* dst, size_t n) {
        size_t can = readable();
        if (n > can) n = can;
        size_t t = _tail.load(std::memory_order_relaxed);
        size_t first = std::min(n, _cap - (t & _mask));
        std::memcpy(dst, _buf + (t & _mask), first * sizeof(T));
        if (n > first) {
            std::memcpy(dst + first, _buf, (n - first) * sizeof(T));
        }
        _tail.store(t + n, std::memory_order_release);
        return n;
    }

private:
    static size_t pow2ceil(size_t x) {
        if (x < 2) return 2;
        --x;
        for (size_t i = 1; i < sizeof(size_t) * 8; i <<= 1) x |= x >> i;
        return x + 1;
    }

    const size_t _cap;
    const size_t _mask;
    T* const _buf;
    std::atomic<size_t> _head; // write index
    std::atomic<size_t> _tail; // read index
};

#endif // SPSC_RING_HPP
