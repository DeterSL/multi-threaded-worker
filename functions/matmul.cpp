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

    std::string image_id_string = "7788787878787887878787878787878";
    std::vector<uint8_t> image_id_data {image_id_string.begin(), image_id_string.end()};

    set_resource(resources.at("image_id").get<std::string>(), image_id_data);

    std::string image_data = values.at("image_data").get<std::string>();

    data = nlohmann::json();
    std::vector<uint8_t> get_image_data = get_resource(resources.at("image_id").get<std::string>());
    std::string get_image_string(get_image_data.begin(), get_image_data.end());
    data["image_id"] = get_image_string;

    std::cout << "the output of function is :" <<  data.dump(2) << std::endl;
  }
  catch(std::exception &e){
    std::cout << e.what() << std::endl;
    return false;
  }
  

  return true;
}
