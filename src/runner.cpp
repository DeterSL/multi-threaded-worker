#include "runner.hpp"
#include "func.hpp"
#include "kv.hpp"
#include <cassert>
#include <iostream>

using namespace verona::rt;
using namespace verona::cpp;

namespace detersl::runner {
   
    Runner::Runner(acquired_cown_span<detersl::types::Resource> cown_arr,
            detersl::func::BasicFuncInfo basic_func) : func_info_(basic_func) {
        std::unordered_map<std::string, detersl::types::Resource*> local_resources;
        for (size_t i = 0; i < func_info_.resources.size(); i++) {
            local_resources[func_info_.resources[i]] = &(*cown_arr.array[i]);
        }

        if (cur_runner != nullptr) {
            // This means that we are changing the current runner ptr
            // without destroying the previous one
            assert(false);
        }
            
        storage = new detersl::kv::ResourceStorage(local_resources);
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
        std::cout << "Runner destroyed at " << this << std::endl;
        cur_runner = nullptr;
    }  
}
