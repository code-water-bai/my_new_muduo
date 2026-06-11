#include "Logging.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <ctime>
#include <unistd.h>
#include <sys/syscall.h>
#include <assert.h>

namespace new_muduo {

    thread_local char t_time[64];
    thread_local time_t t_lastSecond;

    const char* LogLevelName[Logger::NUM_LOG_LEVELS] = {
        "TRACE ", "DEBUG ", "INFO  ", "WARN  ", "ERROR ", "FATAL ",
    };

    Logger::LogLevel g_logLevel = Logger::INFO;

    class T {
    public:
        T(const char* str, unsigned len) : str_(str), len_(len) {}
        const char* str_;
        const unsigned len_;
    };

    inline LogStream& operator<<(LogStream& s, T v) {
        s.buffer().append((std::string)v.str_,(size_t)v.len_);
        return s;
    }

    inline LogStream& operator<<(LogStream& s, const Logger::Impl&) {
        return s;
    }

    void defaultOutput(const char* msg, int len) {
        size_t n = fwrite(msg, 1, len, stdout);
        (void)n;
    }

    void defaultFlush() {
        fflush(stdout);
    }

    Logger::OutputFunc g_output = defaultOutput;
    Logger::FlushFunc g_flush = defaultFlush;

    Logger::Impl::Impl(LogLevel level, int savedErrno, const char* file, int line)
        : time_(Timestamp::now()),
        stream_(),
        level_(level),
        line_(line),
        fullname_(file),
        basename_(nullptr) {
        formatTime();
        stream_ << T(LogLevelName[level], 6);

        const char* slash = strrchr(fullname_, '/');
        if (slash) {
            basename_ = slash + 1;
        }
        else {
            basename_ = fullname_;
        }

        if (savedErrno != 0) {
            stream_ << strerror(savedErrno) << " (errno=" << savedErrno << ") ";
        }
    }

    void Logger::Impl::formatTime() {
        int64_t microSecondsSinceEpoch = time_.microSecondsSinceEpoch();
        time_t seconds = static_cast<time_t>(microSecondsSinceEpoch / Timestamp::kMicroSecondsPerSecond);
        int microseconds = static_cast<int>(microSecondsSinceEpoch % Timestamp::kMicroSecondsPerSecond);
        if (seconds != t_lastSecond) {
            t_lastSecond = seconds;
            struct tm tm_time;
            localtime_r(&seconds, &tm_time);
            int len = snprintf(t_time, sizeof(t_time), "%4d%02d%02d %02d:%02d:%02d",
                tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
                tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);
            assert(len == 17);
        }
        char buf[32];
        snprintf(buf, sizeof(buf), ".%06d ", microseconds);
        stream_ << T(t_time, 17) << T(buf, 8);
    }

    void Logger::Impl::finish() {
        stream_ << " - " << basename_ << ':' << line_ << '\n';
    }

    Logger::Logger(const char* file, int line, LogLevel level)
        : impl_(level, 0, file, line) {
    }

    Logger::Logger(const char* file, int line, LogLevel level, const char* func)
        : impl_(level, 0, file, line) {
        impl_.stream_ << func << ' ';
    }

    Logger::~Logger() {
        impl_.finish();
        const auto& buf = stream().buffer();
        g_output(buf.data(), static_cast<int>(buf.size()));
        if (impl_.level_ == FATAL) {
            g_flush();
            abort();
        }
    }

    void Logger::setLogLevel(LogLevel level) {
        g_logLevel = level;
    }

    void Logger::setOutput(OutputFunc func) {
        g_output = std::move(func);
    }

    void Logger::setFlush(FlushFunc func) {
        g_flush = std::move(func);
    }

    // LogStream formatters
    template <typename T>
    void LogStream::formatInteger(T v) {
        if (buffer_.capacity() - buffer_.size() < kMaxNumericSize) {
            buffer_.reserve(buffer_.size() + kMaxNumericSize);
        }
        char buf[kMaxNumericSize];
        char* p = buf + sizeof(buf);
        bool neg = false;
        if constexpr (std::is_signed_v<T>) {
            if (v < 0) {
                neg = true;
                v = -v;
            }
        }
        do {
            *--p = "0123456789"[v % 10];
            v /= 10;
        } while (v > 0);
        if (neg) *--p = '-';
        buffer_.append(p, buf + sizeof(buf) - p);
    }

    LogStream& LogStream::operator<<(bool v) {
        buffer_.append(v ? "1" : "0");
        return *this;
    }

    LogStream& LogStream::operator<<(short v) { formatInteger(v); return *this; }
    LogStream& LogStream::operator<<(unsigned short v) { formatInteger(v); return *this; }
    LogStream& LogStream::operator<<(int v) { formatInteger(v); return *this; }
    LogStream& LogStream::operator<<(unsigned int v) { formatInteger(v); return *this; }
    LogStream& LogStream::operator<<(long v) { formatInteger(v); return *this; }
    LogStream& LogStream::operator<<(unsigned long v) { formatInteger(v); return *this; }
    LogStream& LogStream::operator<<(long long v) { formatInteger(v); return *this; }
    LogStream& LogStream::operator<<(unsigned long long v) { formatInteger(v); return *this; }
    LogStream& LogStream::operator<<(float v) { buffer_ += std::to_string(v); return *this; }
    LogStream& LogStream::operator<<(double v) { buffer_ += std::to_string(v); return *this; }

    LogStream& LogStream::operator<<(const char* v) {
        if (v) {
            buffer_.append(v);
        }
        else {
            buffer_.append("(null)");
        }
        return *this;
    }

    LogStream& LogStream::operator<<(const std::string& v) {
        buffer_.append(v);
        return *this;
    }

    LogStream& LogStream::operator<<(const void* v) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%p", v);
        buffer_.append(buf);
        return *this;
    }

}  // namespace neo