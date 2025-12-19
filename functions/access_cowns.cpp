#include <iostream>
#include <thread>
#include <nlohmann/json.hpp>
#include "../src/detersl.h"
#include "../src/runner.hpp"
#include "shared_objects.h"

using namespace detersl;

extern "C" int func(std::string input)
{
  Body b{5};
  set_resource<Body>("res3", b);

  std::cout << "res3 is " << get_resource<Body>("res3").x << std::endl;

  delete_resource<Body>("res3");

  return 0;
}
