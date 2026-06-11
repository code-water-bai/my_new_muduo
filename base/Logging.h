#pragma once

#include "noncopyable.h"
#include "Timestamp.h"

#include <string>
#include <functional>
#include <memory>
#define neo new_muduo

namespace new_muduo {

    class LogStream : noncopyable {
    public:
        using self = LogStream;

        self& operator<<(bool v);
        self& operator<<(short v);
        self& operator<<(unsigned short v);
        self& operator<<(int v);
        self& operator<<(unsigned int v);
        self& operator<<(long v);
        self& operator<<(unsigned long v);
        self& operator<<(long long v);
        self& operator<<(unsigned long long v);
        self& operator<<(float v);
        self& operator<<(double v);
        self& operator<<(const char* v);
        self& operator<<(const std::string& v);
        self& operator<<(const void* v);

         std::string& buffer()  { return buffer_; }
        void reset() { buffer_.clear(); }

    private:
        template <typename T>
        void formatInteger(T v);

        std::string buffer_;
        static const int kMaxNumericSize = 48;
    };

    class Logger {
    public:
        enum LogLevel { TRACE, DEBUG, INFO, WARN, ERROR, FATAL, NUM_LOG_LEVELS };

        Logger(const char* file, int line, LogLevel level);
        Logger(const char* file, int line, LogLevel level, const char* func);
        ~Logger();

        LogStream& stream() { return impl_.stream_; }

        static LogLevel logLevel();
        static void setLogLevel(LogLevel level);

        using OutputFunc = std::function<void(const char* msg, int len)>;
        using FlushFunc = std::function<void()>;
        static void setOutput(OutputFunc func);
        static void setFlush(FlushFunc func);

    public:
        class Impl {
        public:
            Impl(LogLevel level, int oldErrno, const char* file, int line);
            void formatTime();
            void finish();

            Timestamp time_;
            LogStream stream_;
            LogLevel level_;
            int line_;
            const char* fullname_;
            const char* basename_;
        };

        Impl impl_;
    };

    extern Logger::LogLevel g_logLevel;

    inline Logger::LogLevel Logger::logLevel() {
        return g_logLevel;
    }

#define LOG_TRACE if (neo::Logger::logLevel() <= neo::Logger::TRACE) \
    new_muduo::Logger(__FILE__, __LINE__, neo::Logger::TRACE, __func__).stream()
#define LOG_DEBUG if (neo::Logger::logLevel() <= neo::Logger::DEBUG) \
    new_muduo::Logger(__FILE__, __LINE__, neo::Logger::DEBUG, __func__).stream()
#define LOG_INFO if (neo::Logger::logLevel() <= neo::Logger::INFO) \
    new_muduo::Logger(__FILE__, __LINE__, neo::Logger::INFO).stream()
#define LOG_WARN neo::Logger(__FILE__, __LINE__, neo::Logger::WARN).stream()
#define LOG_ERROR neo::Logger(__FILE__, __LINE__, neo::Logger::ERROR).stream()
#define LOG_FATAL neo::Logger(__FILE__, __LINE__, neo::Logger::FATAL).stream()
#define LOG_SYSERR neo::Logger(__FILE__, __LINE__, neo::Logger::ERROR).stream()
#define LOG_SYSFATAL neo::Logger(__FILE__, __LINE__, neo::Logger::FATAL).stream()

}  // namespace neo