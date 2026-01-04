#pragma once
#include <unordered_map>
#include "models.hpp"
#include <verona.h>
#include <vector>
#include <cpp/when.h>

using namespace verona::rt;
using namespace verona::cpp;

namespace detersl::worker {
    class RunnerInterface {

        public:

            RunnerInterface() = default;

            virtual std::vector<uint8_t> get_resource(std::string key) = 0;

            virtual void set_resource(std::string key, std::vector<uint8_t> &&data) = 0;

            virtual void delete_resource(std::string key) = 0;

            virtual int run_function(int(*func)(std::string)) = 0;

            virtual ~RunnerInterface() = default;

    };

    inline thread_local RunnerInterface* cur_runner = nullptr;

    class CPPRunner : public RunnerInterface {
        public:
            CPPRunner(acquired_cown_span<Resource> cown_arr, const FunctionState func_state_): func_state(func_state_){
                for(size_t i = 0; i < func_state.resources.size(); i++){
                    local_resources[func_state.resources[i]] = &(*cown_arr.array[i]);
                }
                cur_runner = this;
            }   
 
            std::vector<uint8_t> get_resource(std::string key) override {
                return local_resources.at(key)->get_data();
            }

            void set_resource(std::string key, std::vector<uint8_t> &&data) override
            {
                if(local_resources.find(key) == local_resources.end()) {
                    return;
                }
                local_resources[key]->set_data(std::move(data));
            }

            void delete_resource(std::string key) override
            {
                if(local_resources.find(key) == local_resources.end()) {
                    return;
                }
                local_resources[key]->free_data();
                local_resources.erase(key);
            }
            
            int run_function(int(*func)(std::string)) override {
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

            std::vector<std::string> get_deleted_resources() {
                std::vector<std::string> resources;
                for(auto res: func_state.resources){
                    if(local_resources.find(res) == local_resources.end()){
                        resources.push_back(res);
                    }
                }
                return resources;
            }

            ~CPPRunner() {
                cur_runner = nullptr;
            }  
        
        protected:
            std::unordered_map<std::string, Resource*> local_resources;
            FunctionState func_state;
    };
}