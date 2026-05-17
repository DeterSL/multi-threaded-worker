#pragma once

#include <atomic>
#include <mutex>
#include <queue>

namespace detersl::worker {
    template<typename T>
    class BasicMPSCQueue {
    public:
        void push(T value) {
            std::lock_guard<std::mutex> lock(mtx_);
            queue_.push(std::move(value));
        }

        T pop() {
            std::unique_lock<std::mutex> lock(mtx_);
            if(queue_.empty()) {
                return T();
            }
            T value = std::move(queue_.front());
            queue_.pop();
            return value;
        }

    private:
        std::queue<T> queue_;
        std::mutex mtx_;
    };
}
