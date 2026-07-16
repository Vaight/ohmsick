// hardware refactor completed on 7/16/2026
// docs comments completed on TBD...

// TODO: create GOOD documentation comments for this file

#pragma once

#include <string>
#include <string_view>

#include "backend/serial_port.h"

namespace hardware {

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

}  // namespace hardware
