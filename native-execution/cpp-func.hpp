#pragma once

#include "../src/func.hpp"
#include "../src/types.hpp"
#include <nlohmann/json.hpp>

namespace detersl::types{
    using FunctionOutput = bool;
    using FunctionInput = nlohmann::json;
    using FunctionType = FunctionOutput (*)(FunctionInput);
}

        
namespace detersl {
    namespace func {

        struct CPPFuncInfo : public BasicFuncInfo {
            detersl::types::FunctionInput in;

            explicit CPPFuncInfo(): BasicFuncInfo(){}

            explicit CPPFuncInfo(const BasicFuncInfo& base_info)
                : BasicFuncInfo(base_info) {}

            explicit CPPFuncInfo(const BasicFuncInfo& base_info, detersl::types::FunctionInput in_)
                : BasicFuncInfo(base_info), in(in_) {}
            
            static CPPFuncInfo from_json(const std::string& json) {
                auto basic_info = BasicFuncInfo::from_json(json);

                CPPFuncInfo cpp_info(basic_info);
                auto j = nlohmann::json::parse(json);

                cpp_info.in = j.value("in", detersl::types::FunctionInput{}); 

                return cpp_info;
            }
        };
        
        class CPPFunc {
            public:

                explicit CPPFunc(CPPFuncInfo cpp_func_info) : func_(nullptr), cpp_func_info_(cpp_func_info) {}
                
                explicit CPPFunc(detersl::types::FunctionType func, CPPFuncInfo cpp_func_info) : func_(func), cpp_func_info_(cpp_func_info) {}

                void set_func(detersl::types::FunctionType func) {
                    func_ = func;
                }

                detersl::types::FunctionOutput operator()() {
                    return func_(cpp_func_info_.in);
                }
                
                CPPFuncInfo& info(){
                    return cpp_func_info_;
                }

            private:
                detersl::types::FunctionType func_;
                CPPFuncInfo cpp_func_info_;
        };
    }
}
