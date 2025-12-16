#include <iostream>
#include <unordered_map>
#include <dlfcn.h>
#include <fstream>
#include "basic-net.hpp"
#include "detersl.h"
#include "runner.hpp"
#include "models.hpp"

using namespace verona::rt;
using namespace verona::cpp;
using json = nlohmann::json;

using FunctionType = int(*)(std::string);

std::unordered_map<std::string, FunctionType> all_functions;
std::unordered_map<std::string, cown_ptr<Resource>> resource_map;

void schedule_function(FunctionState func_state){

  size_t num_res = func_state.resources.size();

  cown_ptr<Resource> * resource_array = new cown_ptr<Resource>[num_res];
  for(size_t i = 0; i < func_state.resources.size(); i++){
    auto res_name = func_state.resources[i];
    if(resource_map.find(res_name) == resource_map.end()){
      std::cout << "Creating new cown for resource: " << res_name << std::endl;
      resource_map[res_name] = make_cown<Resource>(nullptr);
    }
    //resource_array[i] = verona::cpp::read(resource_map[res_name]);
    resource_array[i] = resource_map[res_name];
  }
  
  cown_array<Resource> cowns{resource_array, num_res};
  delete[] resource_array;

  // Schedule function with when
  when(cowns) << [func_state](auto c){    
    detersl::worker::Runner runner(c, func_state);

    int ret = runner.run_function(all_functions[func_state.name]);

    if(ret == 0){
      for(const auto& res_name : runner.get_deleted_resources()){
        std::cout << "Deleting resource cown: " << res_name << std::endl;
        resource_map.erase(res_name);
      }
    }

    // if(!out.empty()){
    //   func_state.output = out;
    // }
    // func_state.finished = true;
  };
}

int parse_and_load(char *data)
{
  std::string func_name(data);
  void* handle = dlopen((func_name + ".dylib").c_str(), RTLD_NOW);
  if (!handle) {
    std::cerr << "dlopen error: " << dlerror() << "\n";
    return 1;
  }

  dlerror(); // clear any existing error

  FunctionType fn = (FunctionType)dlsym(handle, "func");
  const char* err = dlerror();
  if (err) {
    std::cerr << "dlsym error: " << err << "\n";
    return 1;
  }

  all_functions[func_name] = fn;

  std::ifstream ifs("../functions/" + func_name + ".json");
  json jf = json::parse(ifs);
  FunctionState f = jf.get<FunctionState>();

  std::cout << "Loaded: " << data << std::endl;


  schedule_function(jf.get<FunctionState>());
  return 0;
}

void register_and_schedule()
{
  int client_fd = listen_and_accept_client_connection(6666);

  while(1) {
    constexpr size_t BUF_SIZE = 128;
    char buffer[BUF_SIZE];
    ssize_t n = ::read(client_fd, buffer, BUF_SIZE);
    if (n < 0) {
      std::perror("read");
      break;
    }
    if (n == 0) {
      std::cout << "Client disconnected.\n";
      break;
    }
    buffer[n-1] = '\0';    
    parse_and_load(buffer);
  }
}

void hardcoded_test()
{
  // parse_and_load((char *)"print_input");
  // parse_and_load((char *)"print_input");
  // parse_and_load((char *)"access_cowns");
  parse_and_load((char *)"access_cowns");
}

int main(int argc, char **argv)
{
  std::cout << "Hello from the detersl-worker with thread id : " << std::this_thread::get_id() << "\n";
  
  ThreadPool<SchedulerThread>& sched = Scheduler::get();
  //Scheduler::set_detect_leaks(true);
  //sched.set_fair(true);
  sched.init(8);

  // Schedule an external thread to play the role of the dispatcher
  when() << [](){
    Scheduler::add_external_event_source();
    
    //std::thread t(register_and_schedule);
    std::thread t(hardcoded_test);
    t.detach();
  };

  sched.run();
}
