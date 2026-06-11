#pragma once

#include "../base/noncopyable.h"

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>

namespace new_muduo {

   
    class Buffer : noncopyable {
    public:
        static const size_t kCheapPrepend = 8;
        static const size_t kInitialSize = 1024;

        explicit Buffer(size_t initialSize = kInitialSize)
            : buffer_(kCheapPrepend + initialSize),
            readerIndex_(kCheapPrepend),
            writerIndex_(kCheapPrepend) {
            assert(readableBytes() == 0);
            assert(writableBytes() == initialSize);
            assert(prependableBytes() == kCheapPrepend);
        }

        void swap(Buffer& rhs) {
            buffer_.swap(rhs.buffer_);
            std::swap(readerIndex_, rhs.readerIndex_);
            std::swap(writerIndex_, rhs.writerIndex_);
        }

        size_t readableBytes() const { return writerIndex_ - readerIndex_; }
        size_t writableBytes() const { return buffer_.size() - writerIndex_; }
        size_t prependableBytes() const { return readerIndex_; }

        const char* peek() const { return begin() + readerIndex_; }

        const char* findCRLF() const {
            const char* crlf = std::search(peek(), beginWrite(), kCRLF, kCRLF + 2);
            return crlf == beginWrite() ? nullptr : crlf;
        }

        const char* findCRLF(const char* start) const {
            assert(peek() <= start);
            assert(start <= beginWrite());
            const char* crlf = std::search(start, beginWrite(), kCRLF, kCRLF + 2);
            return crlf == beginWrite() ? nullptr : crlf;
        }

        const char* findEOL() const {
            const void* eol = memchr(peek(), '\n', readableBytes());
            return static_cast<const char*>(eol);
        }

        const char* findEOL(const char* start) const {
            assert(peek() <= start);
            assert(start <= beginWrite());
            const void* eol = memchr(start, '\n', beginWrite() - start);
            return static_cast<const char*>(eol);
        }

        void retrieve(size_t len) {
            assert(len <= readableBytes());
            if (len < readableBytes()) {
                readerIndex_ += len;
            }
            else {
                retrieveAll();
            }
        }

        void retrieveUntil(const char* end) {
            assert(peek() <= end);
            assert(end <= beginWrite());
            retrieve(end - peek());
        }

        void retrieveInt64() { retrieve(sizeof(int64_t)); }
        void retrieveInt32() { retrieve(sizeof(int32_t)); }
        void retrieveInt16() { retrieve(sizeof(int16_t)); }
        void retrieveInt8() { retrieve(sizeof(int8_t)); }

        void retrieveAll() {
            readerIndex_ = kCheapPrepend;
            writerIndex_ = kCheapPrepend;
        }

        std::string retrieveAllAsString() {
            return retrieveAsString(readableBytes());
        }

        std::string retrieveAsString(size_t len) {
            assert(len <= readableBytes());
            std::string result(peek(), len);
            retrieve(len);
            return result;
        }

        void append(const char* data, size_t len) {
            ensureWritableBytes(len);
            std::copy(data, data + len, beginWrite());
            hasWritten(len);
        }

        void append(const void* data, size_t len) {
            append(static_cast<const char*>(data), len);
        }

        void append(const std::string& str) {
            append(str.data(), str.size());
        }

        void ensureWritableBytes(size_t len) {
            if (writableBytes() < len) {
                makeSpace(len);
            }
            assert(writableBytes() >= len);
        }

        char* beginWrite() { return begin() + writerIndex_; }
        const char* beginWrite() const { return begin() + writerIndex_; }

        void hasWritten(size_t len) {
            assert(len <= writableBytes());
            writerIndex_ += len;
        }

        void unwrite(size_t len) {
            assert(len <= readableBytes());
            writerIndex_ -= len;
        }

        void prepend(const void* data, size_t len) {
            assert(len <= prependableBytes());
            readerIndex_ -= len;
            const char* d = static_cast<const char*>(data);
            std::copy(d, d + len, begin() + readerIndex_);
        }

        void prependInt64(int64_t x) {
            int64_t be = htobe64(x);
            prepend(&be, sizeof(be));
        }
        void prependInt32(int32_t x) {
            int32_t be = htobe32(x);
            prepend(&be, sizeof(be));
        }
        void prependInt16(int16_t x) {
            int16_t be = htobe16(x);
            prepend(&be, sizeof(be));
        }
        void prependInt8(int8_t x) { prepend(&x, sizeof(x)); }

        void shrink(size_t reserve) {
            Buffer other;
            other.ensureWritableBytes(readableBytes() + reserve);
            other.append(peek(), readableBytes());
            swap(other);
        }

        size_t internalCapacity() const { return buffer_.capacity(); }

        ssize_t readFd(int fd, int* savedErrno);

    private:
        char* begin() { return buffer_.data(); }
        const char* begin() const { return buffer_.data(); }

        void makeSpace(size_t len) {
            if (writableBytes() + prependableBytes() < len + kCheapPrepend) {
                buffer_.resize(writerIndex_ + len);
            }
            else {
                assert(kCheapPrepend < readerIndex_);
                size_t readable = readableBytes();
                std::copy(begin() + readerIndex_,
                    begin() + writerIndex_,
                    begin() + kCheapPrepend);
                readerIndex_ = kCheapPrepend;
                writerIndex_ = readerIndex_ + readable;
                assert(readable == readableBytes());
            }
        }

        std::vector<char> buffer_;
        size_t readerIndex_;
        size_t writerIndex_;

        static const char kCRLF[];
    };

}  // namespace neo