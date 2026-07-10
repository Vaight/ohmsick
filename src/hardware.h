#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hardware {

#ifdef _WIN32
using NativeSerialHandle = void*;
constexpr NativeSerialHandle invalidSerialHandle = nullptr;
#else
using NativeSerialHandle = int;
constexpr NativeSerialHandle invalidSerialHandle = -1;
#endif

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
    NativeSerialHandle nativeHandle() const;
    std::string readAvailable() const;
    void write(std::string_view message) const;
    void sendLine(std::string_view message) const;

private:
    NativeSerialHandle handle_ = invalidSerialHandle;
};

NativeSerialHandle openSerialPort(const std::string& device, int baud);
std::string readAvailable(NativeSerialHandle serialHandle);
void writeSerial(NativeSerialHandle serialHandle, std::string_view message);
void sendSerialLine(NativeSerialHandle serialHandle, std::string_view message);
std::optional<Frame> parseFrame(std::string_view line);
std::string formatFrame(const Frame& frame);
ParsedLine parseLine(const std::string& line);
void closeSerialPort(NativeSerialHandle serialHandle);

}  // namespace hardware
