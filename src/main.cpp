#include <iostream>
#include "scheduling.hpp"

using namespace verona::rt;
using namespace verona::cpp;

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
