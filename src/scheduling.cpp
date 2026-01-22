#include "scheduling.hpp"

#include <dlfcn.h>
#include <fstream>
#include "basic-net.hpp"
#include "cpp-func.hpp"
#include "cpp-runner.hpp"
#include "cpp/cown.h"
#include "resource.hpp"
#include "types.hpp"
#include "wasm-func.hpp"
#include "wasm-runner.hpp"
#include "thread-safe-queue.hpp"
#include <chrono>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace detersl::worker {

std::unordered_map<std::string, detersl::types::FunctionType> all_functions;
std::unordered_map<std::string, cown_ptr<detersl::types::Resource>> resource_map;
BasicMPSCQueue<std::string> deleted_resources_queue;

// namespace detersl::worker
void register_function(const std::string& name, detersl::types::FunctionType fn)
{
  all_functions[name] = fn;
}

void schedule_function(detersl::func::WasmFuncInfo func_info, detersl::func::WasmFunc func)
{
  const size_t num_res = func_info.resources.size();
  const size_t num_ro_res = func_info.read_only_resources.size();
  const size_t num_rw_res = num_res - num_ro_res;

  cown_ptr<detersl::types::Resource> read_write_resources[num_rw_res];
  cown_ptr<detersl::types::Resource> read_only_resources[num_ro_res];
  size_t ro_index = 0;
  size_t rw_index = 0;

  for(auto & res_name : func_info.resources){
    if (resource_map.find(res_name) == resource_map.end())
    {
      std::cout << "Creating new cown for resource: " << res_name << std::endl;
      resource_map[res_name] = make_cown<detersl::types::Resource>();
    }
    if(func_info.read_only_resources.find(res_name) != func_info.read_only_resources.end()){
      read_only_resources[ro_index++] = resource_map[res_name];
    }
    else{
      read_write_resources[rw_index++] = resource_map[res_name];
    }
  }

  cown_array<detersl::types::Resource> rw_cowns{read_write_resources, num_rw_res};
  cown_array<detersl::types::Resource> ro_cowns{read_only_resources, num_ro_res};

  when(rw_cowns, read(ro_cowns)) << [func_info, func](auto rw_c, auto ro_c) {
    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();

    for(int i = 0; i < rw_c.length; i++) {
      if(rw_c.array[i]->is_deleted()) {
        std::cerr << "Error: Function " << func_info.func_name 
                  << " tried to access deleted resource." << std::endl;
        return;
      }
    }
    for(int i = 0; i < ro_c.length; i++) {
      if(ro_c.array[i]->is_deleted()) {
        std::cerr << "Error: Function " << func_info.func_name 
                  << " tried to access deleted resource." << std::endl;
        return;
      }
    }
    
    detersl::runner::WasmRunner runner(rw_c, ro_c, func_info, func);

    // TODO: maybe a return code of funciton?
    // Like previous version
    runner.run();
    

    for (const auto& res_name : runner.get_deleted_resources()) {
      deleted_resources_queue.push(res_name);
    }
    std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "Function " << func_info.func_name << " executed in " << duration << " ms\n";
  };
}

std::string read_file_as_string(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + filename);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// int parse_and_load_cpp(const std::string& func_name)
// {
//   void* handle = dlopen(("./" + func_name + ".dylib").c_str(), RTLD_NOW | RTLD_GLOBAL);
//   if (!handle)
//   {
//     std::cerr << "dlopen error: " << dlerror() << "\n";
//     return 1;
//   }

//   dlerror(); // clear any existing error

//   detersl::types::FunctionType fn = (detersl::types::FunctionType)dlsym(handle, "func");
//   const char* err = dlerror();
//   if (err)
//   {
//     std::cerr << "dlsym error: " << err << "\n";
//     return 1;
//   }

//   register_function(func_name, fn);

//   std::ifstream ifs("../functions/" + func_name + ".json");
//   std::string json_config = read_file_as_string("../functions/" + func_name + ".json");
//   detersl::func::CPPFuncInfo f = detersl::func::CPPFuncInfo::from_json(json_config);
//   detersl::func::CPPFunc func(fn, f);

//   std::cout << "Loaded: " << func_name << std::endl;

//   schedule_function(f, func);
//   return 0;
// }

int parse_and_load(const std::string& func_name)
{
  const std::string path = "../functions/" + func_name + ".json";
  std::string json_config;

  try{
    json_config = read_file_as_string(path);
  } catch (const std::runtime_error& e) {
    std::cerr << "Could not find file: " << path << "\n";
    return 1;
  }

  nlohmann::json j;
  try {
    j = nlohmann::json::parse(json_config);
  } catch (const nlohmann::json::parse_error& e) {
    std::cerr << "JSON parse error in " << path << ": " << e.what() << "\n";
    return 1;
  }

  detersl::func::WasmFuncInfo f = detersl::func::WasmFuncInfo::from_json(j);
  detersl::func::WasmFunc func(f);

  std::cout << "Loaded: " << func_name << std::endl;
  schedule_function(f, func);
  return 0;
}

void cleanup_resources()
{
  std::string res_name;
  while(!(res_name = deleted_resources_queue.pop()).empty()){
    std::cout << "Deleting resource: " << res_name << std::endl;
    auto it = resource_map.find(res_name);
    if (it != resource_map.end())
    {
      resource_map.erase(it);
    }
  }
}

void register_and_schedule()
{
  int client_fd = listen_and_accept_client_connection(6666);
  const int delete_after_n_func = 3;
  int func_count = 0;

  while (1)
  {
    constexpr size_t BUF_SIZE = 128;
    char buffer[BUF_SIZE];
    ssize_t n = ::read(client_fd, buffer, BUF_SIZE);
    if (n < 0)
    {
      std::perror("read");
      break;
    }
    if (n == 0)
    {
      std::cout << "Client disconnected.\n";
      break;
    }
    buffer[n - 1] = '\0';

    if(func_count >= delete_after_n_func) {
      func_count = 0;
      cleanup_resources();
    }

    if(parse_and_load(buffer) == 0){
      func_count++;
    }

  }
}

} // namespace detersl::worker