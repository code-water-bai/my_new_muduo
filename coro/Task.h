#include "../base/noncopyable.h"
#include "../pool/CoroutinePool.h"

#include <coroutine>
#include <exception>
#include <utility>
#include <cassert>
#include <optional>

namespace new_muduo {

	template<class T = void>
	class Task;

	namespace detail {
		struct SymmetricTransfer final {
			std::coroutine_handle<> continuation;
			explicit SymmetricTransfer(std::coroutine_handle<> h) : continuation(h) {}
			bool await_ready() const noexcept { return false; }
			std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller)noexcept {
				return continuation;
			}
			void await_resume() noexcept {}
		};

		struct TaskPromiseBase {
			std::coroutine_handle<> continuation_;
			std::exception_ptr eptr_;

			TaskPromiseBase() : continuation_(std::noop_coroutine()) {}
			auto initial_suspend() { return std::suspend_always{}; }
			void unhandled_exception() {
				eptr_ = std::current_exception();
			}
		};

		template<class T>
		struct TaskPromise final : TaskPromiseBase {
			std::optional<T> result_;
			
			Task<T> get_return_object();

			void return_value(T val) {
				result_.emplace(std::move(val));
			}

			auto final_suspend() noexcept {
				return SymmetricTransfer{ continuation_ };
			}

			T get_result() {
				if (eptr_) {
					std::rethrow_exception(eptr_);
				}
				assert(result_.has_value());
				return std::move(*result_);
			}
		};

	}

	template <typename T>
	class /* [[nodiscard]]*/ Task {
	public:
		using promise_type = detail::TaskPromise<T>;
		Task() : handle_(nullptr) {}

		explicit Task(std::coroutine_handle<promise_type> h) : handle_(h) {}

		Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

		Task& operator=(Task&& other) noexcept {
			if (this != &other) {
				destroy();
				handle_ = std::exchange(other.handle_, nullptr);
			}
			return *this;
		}

		Task(const Task&) = delete;
		Task& operator=(const Task&) = delete;

		~Task() { destroy(); }

		auto operator co_await() noexcept {
			struct Awaiter {
				std::coroutine_handle<promise_type> handle_;

				bool await_ready() const noexcept {
					return handle_.done();
				}

				std::coroutine_handle<> await_suspend(
					std::coroutine_handle<> continuation) noexcept {
					handle_.promise().continuation_ = continuation;
					return handle_;
				}

				T await_resume() {
					return handle_.promise().get_result();
				}
			};
			return Awaiter{ handle_ };
		}

		explicit operator bool() const { return handle_ != nullptr; }
		auto handle() const { return handle_; }
	private:
		void destroy() {
			if (handle_) {
				handle_.destroy();
				handle_ = nullptr;
			}
		}

		T syncWait() {
			if (!handle_ || handle_.done()) {
				if constexpr (!std::is_void_v<T>) {
					return handle_.promise().get_result();
				}
				else {
					handle_.promise().get_result();
					return;
				}
			}
			auto& promise = handle_.promise();
			// 启动协程
			handle_.resume();
			// 协程完成时由 SymmetricTransfer 跳回此处
			if constexpr (!std::is_void_v<T>) {
				return promise.get_result();
			}
			else {
				promise.get_result();
				return;
			}
		}

		std::coroutine_handle<promise_type> handle_;

	};

	// TaskPromise<void> 特化必须在 Task 完整定义之后
	namespace detail {
		template <>
		struct TaskPromise<void> final : TaskPromiseBase {
			Task<void> get_return_object() {
				return Task<void>{std::coroutine_handle<TaskPromise<void>>::from_promise(*this)};
			}

			void return_void() {}

			auto final_suspend() noexcept {
				return SymmetricTransfer{ continuation_ };
			}

			void get_result() {
				if (eptr_) {
					std::rethrow_exception(eptr_);
				}
			}
		};

		template <typename T>
		Task<T> TaskPromise<T>::get_return_object() {
			return Task<T>{std::coroutine_handle<TaskPromise<T>>::from_promise(*this)};
		}

	}  // namespace detail

}