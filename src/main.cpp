#include <iostream>
#include "ffi.rs.h"
#include "nats-worker.hpp"
#include <cpp/when.h>
#include "cpp/cown.h"
#include "registrar.hpp"
#include "registry.hpp"
#include <string>

using namespace verona::rt;
using namespace verona::cpp;

const rust::Box<DeterSLEngine> engine = new_detersl_engine(std::string("./config.json"));

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

  detersl::worker::FunctionRegistry function_registry;
  detersl::worker::WorkflowRegistry workflow_registry;
  detersl::worker::FunctionRegistrar function_registrar(function_registry);
  detersl::worker::WorkflowRegistrar workflow_registrar(function_registry, workflow_registry);
  detersl::worker::Scheduling scheduling(workflow_registry);
  // Schedule an external thread to play the role of the dispatcher
  when() << [&scheduling, &function_registrar, &workflow_registrar](){
    Scheduler::add_external_event_source();
    std::thread t(
        detersl::nats::run_nats_worker,
        std::ref(scheduling),
        std::ref(function_registrar),
        std::ref(workflow_registrar));
    t.detach();
  };

  sched.run();
}
