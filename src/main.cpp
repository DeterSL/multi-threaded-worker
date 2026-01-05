#include <iostream>
#include "ffi.rs.h"
#include "scheduling.hpp"
#include "wasm-executioner.hpp"

using namespace verona::rt;
using namespace verona::cpp;

rust::Box<DeterSLEngine> engine = new_detersl_engine(1024);

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
    std::thread t(detersl::worker::hardcoded_test);
    t.detach();
  };

  sched.run();
}
