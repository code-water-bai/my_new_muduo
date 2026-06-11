#ifndef NEO_MUDUO_BASE_NONCOPYABLE_H
#define NEO_MUDUO_BASE_NONCOPYABLE_H

namespace new_muduo {

// 禁止拷贝的基类
class noncopyable {
public:
    noncopyable(const noncopyable&) = delete;
    noncopyable& operator=(const noncopyable&) = delete;
protected:
    noncopyable() = default;
    ~noncopyable() = default;
};

}  // namespace neo_muduo

#endif  // NEO_MUDUO_BASE_NONCOPYABLE_H