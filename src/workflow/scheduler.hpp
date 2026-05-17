#pragma once

#include "cpp/cown.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <utility>

#include "execution/wasm/wasm_function.hpp"
#include "storage/resource.hpp"
#include "support/fast_json.hpp"
#include "support/mpsc_queue.hpp"
#include "workflow/graph.hpp"
#include "workflow/model/invocation.hpp"
#include "workflow/registry.hpp"

namespace detersl::worker {
    
class Scheduling {

    public:
        explicit Scheduling(const WorkflowRegistry& workflows);

        void cleanup_resources();

        bool invoke_workflow(
            detersl::fastjson::InvokeRequest request,
            const uint64_t& id,
            std::string* err);

        bool get_resource_async(
            const std::string& res_name,
            std::function<void(const rust::Vec<uint8_t>&)> on_ready);

        bool get_workflow_status(const std::string& request_id, detersl::types::WorkflowStatus* status);

    private:
        bool schedule_graph(const Node* node,
                            detersl::types::WorkflowInvocation& invocation,
                            std::string* err);

        void schedule_commit_behaviour(detersl::types::WorkflowInvocation& invocation);
        
        bool schedule_choice_node(const Node* node,
                                    detersl::types::WorkflowInvocation& invocation,
                                    verona::cpp::cown_ptr<detersl::types::ChoiceControl> control,
                                    const detersl::types::BranchGuard* guard,
                                    std::string* err);
        
        bool run_task_node(const Node* node,
                            detersl::types::WorkflowInvocation& invocation,
                            const detersl::types::BranchGuard* guard,
                            std::string* err);

        void schedule_function(const detersl::func::WasmFuncInfo& func_info,
                                        detersl::types::WorkflowInvocation& invocation,
                                        const detersl::types::BranchGuard* guard);
        
        const WorkflowRegistry& workflows_;
        std::unordered_map<std::string, std::pair<verona::cpp::cown_ptr<detersl::types::Resource>, uint64_t>> resource_map;
        BasicMPSCQueue<std::pair<std::string, uint64_t>> deleted_resources_queue;
};

} // namespace detersl::worker
