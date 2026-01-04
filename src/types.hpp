#pragma once


#include <string>
namespace detersl {
    namespace types {
        using FunctionOutput = int;
        using FunctionInput = std::string;
        using FunctionType = FunctionOutput (*)(FunctionInput);
    }
}
