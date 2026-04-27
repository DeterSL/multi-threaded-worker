#pragma once
#include <chrono>
#include <mutex>
#include <atomic>
#include <memory>
#include <functional>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace detersl::status {

struct InvocationStatus {
    uint64_t invocation_id;
    std::shared_ptr<std::atomic<bool>> failed;
    int64_t completed_at_ms{-1};

    InvocationStatus(uint64_t invocation_id_,
        std::shared_ptr<std::atomic<bool>> failed_);
    
    InvocationStatus() = default;
    
    void complete();
};

inline void to_json(json& j, const InvocationStatus& v) {
    j = json::object();
    j["request_id"]   = v.invocation_id;
    j["failed"]      =  v.failed->load(std::memory_order_acquire);
    j["completed_at"]   = v.completed_at_ms;
}

using CompletionCallback = std::function<void(InvocationStatus*)>;

void set_completion_callback(CompletionCallback cb);

} // namespace detersl::status
