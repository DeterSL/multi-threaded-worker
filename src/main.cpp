#include <iostream>
#include "ffi.rs.h"
#include "http-server.hpp"
#include "wasm-executioner.hpp"

using namespace verona::rt;
using namespace verona::cpp;

rust::Box<DeterSLEngine> engine = new_detersl_engine(1024);

int main(int argc, char **argv)
{
  ThreadPool<SchedulerThread>& sched = Scheduler::get();
  //Scheduler::set_detect_leaks(true);
  //sched.set_fair(true);
  sched.init(8);

  // Schedule an external thread to play the role of the dispatcher
  when() << [](){
    Scheduler::add_external_event_source();
    
    //std::thread t(register_and_schedule);
    std::thread t(detersl::server::register_and_schedule_json);
    t.detach();
  };

  sched.run();
}
