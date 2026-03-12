#pragma once
#include <chrono>
#include <mutex>
#include <atomic>

namespace detersl::metrics {

struct InvocationMetrics {
    std::chrono::steady_clock::time_point submitted_at = std::chrono::steady_clock::now();
    std::shared_ptr<std::atomic<bool>> failed;
    std::atomic<bool> completed{false};
    std::atomic<int64_t> latency_ms{-1};
    std::atomic<int64_t> completed_at_ms{-1};

    InvocationMetrics(std::chrono::steady_clock::time_point submitted_at_, 
        std::shared_ptr<std::atomic<bool>> failed_);

    void complete();
};

std::shared_ptr<InvocationMetrics> get_invocation_metrics(const std::string& request_id);

void insert_invocation_metric(const std::string& request_id, const std::shared_ptr<InvocationMetrics> metric);

void prune_completed_invocation_metrics();
}
