#include <iostream>
#include <thread>
#include <nlohmann/json.hpp>
#include "../src/detersl.h"
#include "../src/runner.hpp"
#include "shared_objects.h"

using namespace detersl;

extern "C" int func(std::string input)
{
  //Body b{5};
  std::string res3_data = "Hello, DeterSL!";
  std::vector<uint8_t> res3_data_vector(res3_data.begin(), res3_data.end());
  set_resource("res3", std::move(res3_data_vector));

  std::cout << "res2 is ";
  for(const uint8_t byte: get_resource("res2"))
    std::cout << static_cast<int>(byte) << " ";
  std::cout<< std::endl;


  res3_data_vector = get_resource("res3");
  std::string get_res3_data(res3_data_vector.begin(), res3_data_vector.end());

  std::cout << "res3 is " << get_res3_data << std::endl;
  // std::string get_res3_data(get_resource("res3").begin(), get_resource("res3").end());
  // std::cout << "res3 is " << get_res3_data << std::endl;

  delete_resource("res3");

  return 0;
}
