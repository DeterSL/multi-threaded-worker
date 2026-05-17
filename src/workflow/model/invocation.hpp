#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <cpp/cown.h>

#include "storage/resource.hpp"
#include "support/fast_json.hpp"
#include "workflow/status.hpp"

namespace detersl::worker {
struct Node;
}

namespace detersl::types {

struct ChoiceControl {
  int selected = -1;
};

struct BranchGuard {
  verona::cpp::cown_ptr<ChoiceControl> control;
  int edge_index = -1;
};

struct WorkflowInvocation {
  uint64_t invocation_id;
  std::unordered_map<std::string, verona::cpp::cown_ptr<detersl::types::Resource>>
      workflow_resources;
  std::unordered_set<std::string> workflow_rw_resources;
  verona::cpp::cown_ptr<detersl::types::Resource> invocation_cown;
  std::shared_ptr<std::atomic<bool>> failed = std::make_shared<std::atomic<bool>>(false);
  std::shared_ptr<detersl::status::InvocationStatus> status;
  std::shared_ptr<const detersl::worker::Node> workflow_root;
  detersl::fastjson::InvokeRequest request;
};

struct WorkflowStatus {
  bool done = false;
  bool failed = false;
  int64_t latency_ms = -1;
  int64_t completed_at_ms = -1;
};

}  // namespace detersl::types
