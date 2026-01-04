#pragma once

#include <cstdlib>
#include <iostream>
#include <string>

class Body{
public:
  int x;
  Body(int val){
    x = val;
  }

  std::string as_string() {
      return std::to_string(x);
  }

  static Body from_string(std::string str) {
      return Body(std::atoi(str.data()));
  }

  ~Body(){
    std::cout << "Freeing Body in destructor" << std::endl;
  }
};
