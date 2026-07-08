#pragma once

#include <string>

namespace hardware {

struct ParsedLine {
    bool isData = false;
    std::string text;
};

int openSerialPort(const std::string& device, int baud);
std::string readAvailable(int serialFd);
ParsedLine parseLine(const std::string& line);
void closeSerialPort(int serialFd);

}  // namespace hardware
