#include "runner.hpp"

using namespace verona::rt;
using namespace verona::cpp;

namespace detersl::worker {
   
    Runner::Runner(acquired_cown_span<Resource> cown_arr, const FunctionState func_state_): func_state(func_state_){
            for(size_t i = 0; i < func_state.resources.size(); i++){
                local_resources[func_state.resources[i]] = &(*cown_arr.array[i]);
            }
            cur_runner = this;
    }

    int Runner::run_function(int(*func)(std::string)) {
        std::cout << "Running " << func_state.name << " from thread : " << std::this_thread::get_id() << "\n";

        json input_json = func_state;
        try{
            func(input_json.dump());
        } catch(const std::exception& e){
            std::cerr << "Function " << func_state.name << " exited with error: " << e.what() << std::endl;
            return -1;
        }
        return 0;
    }

    std::vector<std::string> Runner::get_deleted_resources() {
        std::vector<std::string> resources;
        for(auto res: func_state.resources){
            if(local_resources.find(res) == local_resources.end()){
                resources.push_back(res);
            }
        }
        return resources;
    }

    Runner::~Runner() {
        cur_runner = nullptr;
    }  
}