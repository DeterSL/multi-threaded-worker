#include <iostream>
#include <thread>
#include <nlohmann/json.hpp>
#include "../src/detersl.h"
#include "../src/runner.hpp"
#include "shared_objects.h"

using namespace detersl;

extern "C" int func(std::string input)
{
  make_resource<int>("res1", 8);
  make_resource<std::string>("res2", std::string("Hello World!"));

  // std::cout << "res1 is " << get_resource<int>("res1") << " and res2 is " <<
  // get_resource<std::string>("res2") << std::endl;

  std::this_thread::sleep_for(std::chrono::milliseconds(5000));

  std::cout << "slept for 5 seconds" <<std::endl;

  return 0;
}
