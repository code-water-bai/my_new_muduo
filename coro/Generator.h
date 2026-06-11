#pragma once

#include <coroutine>
#include <exception>
#include <utility>


namespace new_muduo {
	template<class T>
	class Generator {
		struct promise_type;
		using handle_type = std::coroutine_handle<promise_type>;

		struct promise_type {
			T value_;
			std::exception_ptr eptr_;

			Generator get_return_object() {
				return Generator(handle_type::from_promise(*this));
			}

			std::suspend_always initial_suspend() { return {}; }
			std::suspend_always final_suspend() { return{}; }
			std::suspend_always yield_value(T value) noexcept {
				value_ = std::move(value);
				return {};
			}
			void return_void() {}
			void unhandled_exception() noexcept { eptr_ = std::current_exception(); }
		};

		struct iterator {
			handle_type handle_;
			
			T& operator*() {
				return handle_.promise().value_;
			}
			iterator& operator ++ () {
				handle_.resume();
				if (handle_.promise().eptr_)
					std::rethrow_exception(handle_.promise().eptr_);
				return *this;
			}

			bool operator!=(std::default_sentinel_t) const {
				return !handle_.done();
			}
		};
	private:
		handle_type handle_;
	public:
		explicit  Generator(handle_type h) noexcept :handle_(h) {}
		Generator(Generator&& other) noexcept
			: handle_(std::exchange(other.handle_, nullptr)) {}

		Generator& operator=(Generator&& other) noexcept {
			if (this != &other) {
				if (handle_) handle_.destroy();
				handle_ = std::exchange(other.handle_, nullptr);
			}
			return *this;
		}

		Generator(const Generator&) = delete;
		Generator& operator=(const Generator&) = delete;

		~Generator() {
			if (handle_) handle_.destroy();
		}

		iterator begin() {
			if (handle_) {
				handle_.resume();  // 启动到第一个 co_yield
				if (handle_.promise().eptr_)
					std::rethrow_exception(handle_.promise().eptr_);
			}
			return iterator{ handle_ };
		}

		std::default_sentinel_t end() { return {}; }
	};
}