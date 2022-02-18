
#include <sstream>
#include <iostream>

// This namespace does NOT use any xml library
namespace XML {

    std::string get_element_value(std::string xml, std::string element) {
        std::istringstream f(xml);
        std::string line;

        std::string value;

        while (std::getline(f, line)) {
            if (line.find(element) != std::string::npos) {
                unsigned first_del = line.find("<"+element+">");
                unsigned last_del = line.find("</"+element+">");

                value = line.substr(first_del+element.length()+2, last_del-first_del-element.length()-2);
            }
        }

        return value;
    }

}
