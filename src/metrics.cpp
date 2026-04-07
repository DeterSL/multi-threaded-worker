#include "metrics.hpp"
#include <unordered_map>
#include <string>

namespace detersl::metrics{
  
InvocationMetrics::InvocationMetrics(uint64_t invocation_id_, std::chrono::steady_clock::time_point submitted_at_, 
          std::shared_ptr<std::atomic<bool>> failed_,
          std::function<void(const uint64_t& request_id,
                             bool failed,
                             int64_t latency_ms,
                             int64_t completed_at_ms)> on_complete_): 
          invocation_id(invocation_id_),
          submitted_at(submitted_at_),
          failed(failed_),
          on_complete(std::move(on_complete_)) {}

void InvocationMetrics::complete(){
  bool expected = false;
  if (completed.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
      const auto completed_at = std::chrono::time_point_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now()
      ).time_since_epoch().count();
      completed_at_ms.store(static_cast<int64_t>(completed_at), std::memory_order_release);
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - submitted_at
      ).count();
      latency_ms.store(static_cast<int64_t>(elapsed), std::memory_order_release);
  }
  on_complete(
    invocation_id,
    failed->load(std::memory_order_acquire),
    latency_ms.load(std::memory_order_acquire),
    completed_at_ms.load(std::memory_order_acquire));
}

// std::mutex metrics_mutex;
// std::unordered_map<std::string, std::shared_ptr<InvocationMetrics>> invocation_metrics_index;

// static inline std::shared_ptr<InvocationMetrics> get_invocation_metrics(const std::string& request_id) {
//   std::lock_guard<std::mutex> lock(metrics_mutex);
//   auto it = invocation_metrics_index.find(request_id);
//   if (it == invocation_metrics_index.end()) {
//     return nullptr;
//   }
//   return it->second;
// }

// void insert_invocation_metric(const std::string& request_id, const std::shared_ptr<InvocationMetrics> metric){
//     std::lock_guard<std::mutex> lock(metrics_mutex);
//     invocation_metrics_index[request_id] = metric;
// }

// void prune_completed_invocation_metrics() {
//   constexpr size_t max_tracked_invocations = 10000000;
//   std::lock_guard<std::mutex> lock(metrics_mutex);
//   if (invocation_metrics_index.size() <= max_tracked_invocations) {
//     return;
//   }
//   for (auto it = invocation_metrics_index.begin();
//        it != invocation_metrics_index.end() && invocation_metrics_index.size() > max_tracked_invocations;) {
//     if (it->second->completed.load(std::memory_order_acquire)) {
//       it = invocation_metrics_index.erase(it);
//       continue;
//     }
//     ++it;
//   }
// }

// bool get_workflow_status(const std::string& request_id, detersl::types::WorkflowStatus* status) {
//   auto metrics = get_invocation_metrics(request_id);
//   if (!metrics) {
//     return false;
//   }

//   if (status) {
//     status->done = metrics->completed.load(std::memory_order_acquire);
//     status->failed = metrics->failed && metrics->failed->load(std::memory_order_acquire);
//     status->latency_ms = metrics->latency_ms.load(std::memory_order_acquire);
//     status->completed_at_ms = metrics->completed_at_ms.load(std::memory_order_acquire);
//   }
//   return true;
// }

}
