#include "metrics.hpp"
#include <unordered_map>
#include <string>

namespace detersl::metrics{

CompletionCallback completion_cb_;

void set_completion_callback(CompletionCallback cb) {completion_cb_ = std::move(cb);}
  
InvocationMetrics::InvocationMetrics(uint64_t invocation_id_,
          std::shared_ptr<std::atomic<bool>> failed_): 
          invocation_id(invocation_id_),
          failed(failed_) {}

void InvocationMetrics::complete(){
    completed_at_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now()
    ).time_since_epoch().count();
  
    completion_cb_(this);  
}
}
