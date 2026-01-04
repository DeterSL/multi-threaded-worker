#include <iostream>
#include <thread>
#include <nlohmann/json.hpp>
#include "../src/detersl.h"
#include "../src/runner.hpp"
#include "shared_objects.h"

using namespace detersl;

extern "C" int func(std::string input)
{

  set_resource("res1", {8});
  std::vector<uint8_t> res2_data = {1,2,4};
  set_resource("res2", std::move(res2_data));

  // std::cout << "res1 is " << static_cast<int>(get_resource("res1")[0]) << " and res2 is " <<
  // get_resource("res2")[0] << std::endl;

  // std::this_thread::sleep_for(std::chrono::milliseconds(5000));

  // std::cout << "slept for 5 seconds" <<std::endl;

  return 0;
}