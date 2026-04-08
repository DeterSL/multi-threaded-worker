#pragma once
#include <chrono>
#include <mutex>
#include <atomic>
#include <memory>
#include <functional>

namespace detersl::metrics {

struct InvocationMetrics {
        uint64_t invocation_id;
        std::shared_ptr<std::atomic<bool>> failed;
        std::atomic<bool> completed{false};
        std::atomic<int64_t> completed_at_ms{-1};
        std::function<void(const uint64_t& request_id,
                           bool failed,
                           int64_t completed_at_ms)> on_complete;

        InvocationMetrics(uint64_t invocation_id_,
            std::shared_ptr<std::atomic<bool>> failed_,
            std::function<void(const uint64_t& request_id,
                               bool failed,
                               int64_t completed_at_ms)> on_complete_ = {});

        void complete();
    };



// void prune_completed_invocation_metrics();
}
