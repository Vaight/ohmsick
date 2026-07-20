// hardware refactor completed on 7/16/2026
// docs comments completed on TBD...

// TODO: create GOOD documentation comments for this file

#include "backend/serial_reader.h"

#include <stdexcept>
#include <utility>

namespace backend {

SerialReader::SerialReader(const std::string& device, int baud) {
    open(device, baud);
}

SerialReader::~SerialReader() {
    close();
}

SerialReader::SerialReader(SerialReader&& other) noexcept : handle_(other.handle_) {
    other.handle_ = invalidSerialHandle;
}

SerialReader& SerialReader::operator=(SerialReader&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        other.handle_ = invalidSerialHandle;
    }

    return *this;
}

void SerialReader::open(const std::string& device, int baud) {
    close();
    handle_ = openSerialPort(device, baud);
}

void SerialReader::close() {
    if (handle_ != invalidSerialHandle) {
        closeSerialPort(handle_);
        handle_ = invalidSerialHandle;
    }
}

bool SerialReader::isOpen() const {
    return handle_ != invalidSerialHandle;
}

NativeSerialHandle SerialReader::nativeHandle() const {
    return handle_;
}

std::string SerialReader::readAvailable() const {
    if (handle_ == invalidSerialHandle) {
        return {};
    }

    return backend::readAvailable(handle_);
}

void SerialReader::write(std::string_view message) const {
    if (handle_ == invalidSerialHandle) {
        throw std::runtime_error("Serial write failed: port is not open");
    }

    writeSerial(handle_, message);
}

void SerialReader::sendLine(std::string_view message) const {
    if (handle_ == invalidSerialHandle) {
        throw std::runtime_error("Serial write failed: port is not open");
    }

    sendSerialLine(handle_, message);
}

}  // namespace backend
