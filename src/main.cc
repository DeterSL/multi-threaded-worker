#include <iostream>

#include <verona.h>
#include <cpp/when.h>

using namespace verona::rt;
using namespace verona::cpp;

class Body
{
  int val;
  public:
  Body(int val_) : val(val_)
  {}

  void fn()
  {
    std::cout << "Calling a generic object function\n";
  }
};

// Let's createa cown that protects a file on the local filesystem
cown_ptr<int> create_fs_cown()
{
  const char* path = "/tmp/foo.txt";

  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd == -1) {
    perror("open");
  }

  return make_cown<int>(fd);
}

void scheduler_main()
{
  std::cout << "Hello from the dispatcher.\n";

  // Let's create a few cowns that protect different resources
  // First a cown around a local object
  auto local_memory_cown = make_cown<Body>(42);

  when(local_memory_cown) << [](auto b){ b->fn(); };

  // Now one around a local file
  auto fs_cown = create_fs_cown();

  when(fs_cown) << [](auto fd){
    const char* msg = "Hello from a file cown!\n";
    ssize_t bytes = write(fd, msg, strlen(msg));
  };
}


int main(int argc, char **argv)
{
  std::cout << "Hello from the detersl-worker\n";

  auto& sched = Scheduler::get();
  //Scheduler::set_detect_leaks(true);
  //sched.set_fair(true);
  sched.init(8);

  // Schedule an external thread to play the role of the dispatcher
  when() << [](){
    Scheduler::add_external_event_source();
    scheduler_main();
  };

  sched.run();
}
