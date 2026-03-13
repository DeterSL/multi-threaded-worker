#include <iostream>
#include "ffi.rs.h"
#include "http-server.hpp"
#include "wasm-executioner.hpp"

using namespace verona::rt;
using namespace verona::cpp;

rust::Box<DeterSLEngine> engine = new_detersl_engine(1024);

int main(int argc, char **argv)
{
  size_t num_threads = 8;
  if(argc == 2){
    try{
      num_threads = std::stoi(argv[1]);
    }
    catch(std::exception &e){
      std::cout << "Could not parse argument " << argv[1] << std::endl;
      return 1;
    }
  }

  ThreadPool<SchedulerThread>& sched = Scheduler::get();

  sched.init(num_threads);

  // Schedule an external thread to play the role of the dispatcher
  when() << [](){
    Scheduler::add_external_event_source();
    
    std::thread t(detersl::server::register_and_schedule_json);
    t.detach();
  };

  sched.run();
}
