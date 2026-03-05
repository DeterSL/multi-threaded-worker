#include <iostream>
#include <string>
#include <thread>
#include <nlohmann/json.hpp>
#include "../native-execution/detersl.h"
#include <vector>

using namespace detersl;

extern "C" bool func(nlohmann::json input)
{
  try{
    nlohmann::json data = input["data"];
    nlohmann::json resources = data["resources"];
    nlohmann::json values = data["values"];

    std::string image_data = values.at("image_data").get<std::string>();

    std::vector<uint8_t> res1_v {image_data.begin(),image_data.end()};
    
    set_resource(resources.at("res1").get<std::string>(), res1_v);

    std::vector<uint8_t> get_res1 = get_resource(resources.at("res1").get<std::string>());

    std::string get_res1_s(get_res1.begin(), get_res1.end());

    std::cout << "res1 is " << get_res1_s << std::endl;
  }
  catch(...){
    std::cout << "error in function print input" << std::endl;
    return false;
  }
 
  return true;
}
