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

    int crop_size = values.at("crop_size").get<int>();

    data = nlohmann::json();
    data["image_id"] = crop_size;;

    std::cout << "the output of function is :" <<  data.dump(2) << std::endl;
  }
  catch(std::exception &e){
    std::cout << e.what() << std::endl;
    return false;
  }

  return true;
}
