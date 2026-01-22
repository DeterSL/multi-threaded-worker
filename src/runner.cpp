#include "runner.hpp"
#include "func.hpp"
#include "kv.hpp"
#include <cassert>
#include <iostream>
#include <memory>

using namespace verona::rt;
using namespace verona::cpp;

namespace detersl::runner {

    Runner::Runner(acquired_cown_span<detersl::types::Resource> rw_cown_arr,
            acquired_cown_span<const detersl::types::Resource> ro_cown_arr,
            detersl::func::BasicFuncInfo basic_func) : func_info_(basic_func) {
        std::unordered_map<std::string, detersl::types::Resource*> local_resources;
        for (auto &res_name : func_info_.resources) {
            size_t ro_index = 0;
            size_t rw_index = 0;
            if(func_info_.read_only_resources.find(res_name) != func_info_.read_only_resources.end()){
                // This is a read-only resource
                local_resources[res_name] = const_cast<detersl::types::Resource*>(&(*ro_cown_arr.array[ro_index++]));
            }
            else{
                // This is a read-write resource
                local_resources[res_name] = &(*rw_cown_arr.array[rw_index++]);
            }
        }

        if (cur_runner != nullptr) {
            // This means that we are changing the current runner ptr
            // without destroying the previous one
            assert(false);
        }
            
        storage = std::make_unique<detersl::kv::ResourceStorage>(std::move(local_resources),  
            func_info_.read_only_resources);
        std::cout << "Runner created in thread: " << std::this_thread::get_id() << "\n";
        cur_runner = this;
    }

    std::vector<std::string> Runner::get_deleted_resources() {
        std::vector<std::string> deleted_resources;
        for(auto& res : func_info_.resources) {
            if (storage->get_resource(res) == nullptr) {
                deleted_resources.push_back(res);
            }
        }
        return deleted_resources;
    }

    Runner::~Runner() {
        cur_runner = nullptr;
    }  
}
