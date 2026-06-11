#include "Timestamp.h"
using namespace new_muduo;

new_muduo::Timestamp new_muduo::Timestamp::now()
{
	auto now = std::chrono::system_clock::now();
	auto duration = now.time_since_epoch();
	auto micros = std::chrono::duration_cast<std::chrono::microseconds>(duration);
	return Timestamp(micros.count());
}

std::string Timestamp::toString() const {
    char buf[32] = { 0 };
    int64_t seconds = microSecondsSinceEpoch_ / kMicroSecondsPerSecond;
    int64_t microseconds = microSecondsSinceEpoch_ % kMicroSecondsPerSecond;

    std::snprintf(buf, sizeof(buf), "%lld.%06lld",
        static_cast<long long>(seconds),
        static_cast<long long>(microseconds));
    return buf;
}


std::string Timestamp::toFormattedString(bool showMicroseconds) const {
    time_t seconds = static_cast<time_t>(
        microSecondsSinceEpoch_ / kMicroSecondsPerSecond
        );

    struct tm tm_time;
#ifdef _WIN32
    localtime_s(&tm_time, &seconds);
#else
    localtime_r(&seconds, &tm_time);
#endif

    char buf[64] = { 0 };

    if (showMicroseconds) {
        int microseconds = static_cast<int>(
            microSecondsSinceEpoch_ % kMicroSecondsPerSecond
            );
        std::snprintf(buf, sizeof(buf),
            "%04d%02d%02d %02d:%02d:%02d.%06d",
            tm_time.tm_year + 1900,  // 从 1900 年起算
            tm_time.tm_mon + 1,      // 月份从 0 开始
            tm_time.tm_mday,
            tm_time.tm_hour,
            tm_time.tm_min,
            tm_time.tm_sec,
            microseconds);
    }
    else {
        std::snprintf(buf, sizeof(buf),
            "%04d%02d%02d %02d:%02d:%02d",
            tm_time.tm_year + 1900,
            tm_time.tm_mon + 1,
            tm_time.tm_mday,
            tm_time.tm_hour,
            tm_time.tm_min,
            tm_time.tm_sec);
    }

    return buf;
}

