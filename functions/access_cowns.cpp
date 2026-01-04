#include <iostream>
#include <thread>
#include <nlohmann/json.hpp>
#include "detersl.h"
#include "shared_objects.h"

using namespace detersl;

extern "C" int func(std::string input)
{
  Body b{5};

  set_resource("res3", b.as_string());

  std::cout << "res3 is " << Body::from_string(get_resource("res3")).x << std::endl;

  delete_resource("res3");

  return 0;
}
