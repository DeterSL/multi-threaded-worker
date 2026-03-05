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

    std::string check_str = "checkistrue";
    std::vector<uint8_t> check_data {check_str.begin(),check_str.end()};
    set_resource(resources.at("check").get<std::string>(), check_data);
    

    data = nlohmann::json();
    std::vector<uint8_t> get_check_data = get_resource(resources.at("check").get<std::string>());
    std::string get_check_string(get_check_data.begin(), get_check_data.end());
    data["check"] = get_check_string;
    std::cout << "the output of function is :" <<  data.dump(2) << std::endl;
  }
  catch(std::exception &e){
    std::cout << e.what() << std::endl;
    return false;
  }
  
  return true;
}
