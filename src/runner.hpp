#pragma once
#include <unordered_map>
#include "models.hpp"
#include <verona.h>
#include <cpp/when.h>

using namespace verona::rt;
using namespace verona::cpp;

namespace detersl::worker {
    class Runner {
        public:
            Runner(acquired_cown_span<Resource> cown_arr, const FunctionState func_state_);
 
            template<typename T>
            T& get_resource(const std::string& name) {
                return local_resources.at(name)->template get_data<T>();
            }

            template<class T>
            void make_resource(const std::string &name, T& value)
            {
                if(local_resources.find(name) == local_resources.end()) {
                    return;
                }
                T* stored = new T(value);
                local_resources[name]->set_data(reinterpret_cast<void*>(stored));
            }

            template<class T>
            void delete_resource(const std::string &name)
            {
                if(local_resources.find(name) == local_resources.end()) {
                    return;
                }
                local_resources[name]->template free_data<T>();
                local_resources.erase(name);
            }
            
            int run_function(int(*func)(std::string));

            std::vector<std::string> get_deleted_resources();

            ~Runner();
        
        private:
            std::unordered_map<std::string, Resource*> local_resources;
            FunctionState func_state;
    };

    inline thread_local Runner* cur_runner = nullptr;
}