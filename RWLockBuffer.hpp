#ifndef RW_LOCK_BUFFER_H
#define RW_LOCK_BUFFER_H

#include <boost/thread.hpp>

#include <cmath>

#undef min

class RWLockBuffer {
private:
    size_t _size;
public:
    RWLockBuffer(size_t size) : _size(size) {
        buffer_.resize(size);
        read_count_ = 0;
    }

    template<typename T>
    void write_lock(T f) {
        boost::mutex::scoped_lock lock(lock_);
        cond_.wait(lock, [&]() { return read_count_ == 0; });
        f(buffer_.data(), _size);
        read_count_++;
    }

    void read_lock(char* dest, size_t size, size_t offset = 0) {
        boost::mutex::scoped_lock lock(lock_);
        cond_.wait(lock, [&]() { return read_count_ > 0; });

        size_t s = std::min(size, _size);
        memcpy(dest, buffer_.data() + offset, s);

        read_count_--;
        cond_.notify_one();
    }

private:
    std::vector<char> buffer_;
    int read_count_;
    boost::mutex lock_;
    boost::condition_variable cond_;
};


#endif