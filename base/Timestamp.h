#ifndef NEO_MUDUO_BASE_TIMESTAMP_H
#define NEO_MUDUO_BASE_TIMESTAMP_H

#include <cstdint>
#include <string>
#include <chrono>
#include "noncopyable.h"

namespace new_muduo {

class Timestamp {
public:
    Timestamp() : microSecondsSinceEpoch_(0) {}

    explicit Timestamp(int64_t microSecondsSinceEpoch)
        : microSecondsSinceEpoch_(microSecondsSinceEpoch) {}

    // 获取当前时间
    static Timestamp now();

    // 时间转换
    int64_t microSecondsSinceEpoch() const { return microSecondsSinceEpoch_; }

    // 格式化为字符串
    std::string toString() const;
    std::string toFormattedString(bool showMicroseconds = true) const;

    // 比较操作
    bool operator<(Timestamp rhs) const {
        return microSecondsSinceEpoch_ < rhs.microSecondsSinceEpoch_;
    }
    bool operator==(Timestamp rhs) const {
        return microSecondsSinceEpoch_ == rhs.microSecondsSinceEpoch_;
    }

    // 算术操作
    Timestamp operator+(double seconds) const {
        return Timestamp(microSecondsSinceEpoch_ +
                         static_cast<int64_t>(seconds * kMicroSecondsPerSecond));
    }
    double operator-(Timestamp rhs) const {
        int64_t diff = microSecondsSinceEpoch_ - rhs.microSecondsSinceEpoch_;
        return static_cast<double>(diff) / kMicroSecondsPerSecond;
    }

    static const int kMicroSecondsPerSecond = 1000 * 1000;

    static Timestamp invaild() { return Timestamp(); }
    bool valid() const { return microSecondsSinceEpoch_ > 0; }

private:
    int64_t microSecondsSinceEpoch_;
};

inline Timestamp addTime(Timestamp timestamp, double seconds) {
    return timestamp + seconds;
}

}  // namespace neo_muduo

#endif  // NEO_MUDUO_BASE_TIMESTAMP_H