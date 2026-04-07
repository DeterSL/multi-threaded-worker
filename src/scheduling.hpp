#pragma once

#include "func.hpp"
#include "graph.hpp"
#include "resource.hpp"
#include "runner.hpp"
#include "types.hpp"
#include <cpp/when.h>
#include "cpp/cown.h"
#include <unordered_map>
#include <wasm-func.hpp>
#include "rust/cxx.h"
#include <future>
#include <cstdint>
#include <dlfcn.h>
#include <cctype>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <functional>
#include "metrics.hpp"
#include <deque>
#include <vector>
#include <fstream>
#include "wasm-runner.hpp"
#include "thread-safe-queue.hpp"
#include "metrics.hpp"
#include "utils.hpp"
#include <nlohmann/json.hpp>

using namespace verona::rt;
using namespace verona::cpp;

namespace detersl::worker {
    
class Scheduling {

    public:
        using CompletionCallback = std::function<void(const uint64_t& request_id,
                                                     bool failed,
                                                     int64_t latency_ms,
                                                     int64_t completed_at_ms)>;

        void set_completion_callback(CompletionCallback cb) { completion_cb_ = std::move(cb); }

        void cleanup_resources();

        bool register_wasm_function(const nlohmann::json& j, std::string* err);

        bool register_workflow(const detersl::types::Workflow& workflow, std::string* err);

        bool invoke_workflow(const detersl::types::InvokeDTO& request, uint64_t& id, std::string* err);

        bool get_resource(const std::string& res_name,  std::future<rust::Vec<uint8_t>>& res_data);

        bool get_workflow_status(const std::string& request_id, detersl::types::WorkflowStatus* status);

    private:

        bool schedule_graph(Node* node,
                            detersl::types::WorkflowInvocation& invocation,
                            std::string* err);
        
        void schedule_commit_behaviour(detersl::types::WorkflowInvocation& invocation);

        bool schedule_choice_node(const Node* node,
                                    detersl::types::WorkflowInvocation& invocation,
                                    cown_ptr<detersl::types::ChoiceControl> control,
                                    const detersl::types::BranchGuard* guard,
                                    std::string* err);
        
        bool run_task_node(Node* node,
                            detersl::types::WorkflowInvocation& invocation,
                            const detersl::types::BranchGuard* guard,
                            std::string* err);

        void schedule_function(detersl::func::WasmFuncInfo&& func_info,
                                        detersl::types::WorkflowInvocation& invocation,
                                        const detersl::types::BranchGuard* guard);
        
        std::unordered_map<std::string, std::pair<cown_ptr<detersl::types::Resource>, uint64_t>> resource_map;
        BasicMPSCQueue<std::pair<std::string, uint64_t>> deleted_resources_queue;
        std::unordered_map<std::string, detersl::func::WasmFuncInfo> wasm_func_registry;
        std::unordered_map<std::string, Node*> workflow_registry;
        int next_workflow_request_id = 1;
        CompletionCallback completion_cb_;
};

} // namespace detersl::worker
