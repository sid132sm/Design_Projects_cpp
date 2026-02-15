#include <iostream>
#include "string_parsing.h"

int main() {
    auto instance = VehicleDataParser::getInstance();
    try {
        if(instance == nullptr) {
            std::cerr << "Failed to create VehicleDataParser instance" << std::endl;
            return 1;
        }
        
        if(instance->parseAndSend() != sendStatus::E_OK) {
            std::cerr << "Failed to parse and send vehicle data" << std::endl;
            return 1;
        }
    } catch (const std::exception& ex) {
        std::cerr << "Exception occurred: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
