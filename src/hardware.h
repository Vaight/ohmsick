#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hardware {

constexpr int maxInputSlots = 64;

enum class Kind {
    Pot,
    Button
};

struct Reading {
    int slot = 0;
    int pin = 0;
    Kind kind = Kind::Pot;
    float normalizedValue = 0.0f;
};

struct Frame {
    int expectedCount = 0;
    std::vector<Reading> readings;
};

struct ParsedLine {
    bool isData = false;
    std::string text;
};

class SerialReader {
public:
    SerialReader() = default;
    SerialReader(const std::string& device, int baud);
    ~SerialReader();

    SerialReader(const SerialReader&) = delete;
    SerialReader& operator=(const SerialReader&) = delete;

    SerialReader(SerialReader&& other) noexcept;
    SerialReader& operator=(SerialReader&& other) noexcept;

    void open(const std::string& device, int baud);
    void close();
    bool isOpen() const;
    int nativeHandle() const;
    std::string readAvailable() const;

private:
    int fd_ = -1;
};

int openSerialPort(const std::string& device, int baud);
std::string readAvailable(int serialFd);
std::optional<Frame> parseFrame(std::string_view line);
std::string formatFrame(const Frame& frame);
ParsedLine parseLine(const std::string& line);
void closeSerialPort(int serialFd);

}  // namespace hardware
