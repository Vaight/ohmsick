#include <cerrno>
#include <charconv>
#include <cmath>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cstring>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

#include "hardware.h"

namespace {

#ifdef _WIN32
DWORD baudToSpeed(int baud) {
    switch (baud) {
        case 9600:
        case 19200:
        case 38400:
        case 57600:
        case 115200:
            return static_cast<DWORD>(baud);
        default:
            throw std::runtime_error("Unsupported baud rate. Try 9600, 19200, 38400, 57600, or 115200.");
    }
}

std::string windowsErrorMessage(const std::string& prefix) {
    const DWORD error = GetLastError();
    LPSTR message = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&message),
        0,
        nullptr);

    std::string result = prefix + ": ";
    if (length != 0 && message != nullptr) {
        result.append(message, length);
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
            result.pop_back();
        }
    } else {
        result += "Windows error " + std::to_string(error);
    }

    if (message != nullptr) {
        LocalFree(message);
    }

    return result;
}

std::string normalizeWindowsDeviceName(const std::string& device) {
    if (device.rfind("\\\\.\\", 0) == 0) {
        return device;
    }

    return "\\\\.\\" + device;
}
#else
speed_t baudToSpeed(int baud) {
    switch (baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        default:
            throw std::runtime_error("Unsupported baud rate. Try 9600, 19200, 38400, 57600, or 115200.");
    }
}
#endif

bool parseInt(std::string_view text, int& value) {
    if (text.empty()) {
        return false;
    }

    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc{} && result.ptr == end;
}

float clamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

std::string_view trimLineEnding(std::string_view line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.remove_suffix(1);
    }

    return line;
}

}  // namespace

namespace hardware {

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

    return hardware::readAvailable(handle_);
}

NativeSerialHandle openSerialPort(const std::string& device, int baud) {
#ifdef _WIN32
    const std::string normalizedDevice = normalizeWindowsDeviceName(device);
    HANDLE handle = CreateFileA(
        normalizedDevice.c_str(),
        GENERIC_READ,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (handle == INVALID_HANDLE_VALUE) {
        throw std::runtime_error(windowsErrorMessage("Could not open " + device));
    }

    DCB dcb {};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(handle, &dcb)) {
        CloseHandle(handle);
        throw std::runtime_error(windowsErrorMessage("Could not read serial settings"));
    }

    dcb.BaudRate = baudToSpeed(baud);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;

    if (!SetCommState(handle, &dcb)) {
        CloseHandle(handle);
        throw std::runtime_error(windowsErrorMessage("Could not apply serial settings"));
    }

    COMMTIMEOUTS timeouts {};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;

    if (!SetCommTimeouts(handle, &timeouts)) {
        CloseHandle(handle);
        throw std::runtime_error(windowsErrorMessage("Could not apply serial timeouts"));
    }

    PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return handle;
#else
    const int fd = open(device.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd == -1) {
        throw std::runtime_error("Could not open " + device + ": " + std::strerror(errno));
    }

    termios tty{};
    if (tcgetattr(fd, &tty) != 0) {
        close(fd);
        throw std::runtime_error("Could not read serial settings: " + std::string(std::strerror(errno)));
    }

    cfmakeraw(&tty);
    cfsetispeed(&tty, baudToSpeed(baud));
    cfsetospeed(&tty, baudToSpeed(baud));

    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        close(fd);
        throw std::runtime_error("Could not apply serial settings: " + std::string(std::strerror(errno)));
    }

    tcflush(fd, TCIOFLUSH);
    return fd;
#endif
}

std::string readAvailable(NativeSerialHandle serialHandle) {
#ifdef _WIN32
    char buffer[256];
    DWORD count = 0;
    if (!ReadFile(serialHandle, buffer, sizeof(buffer), &count, nullptr)) {
        throw std::runtime_error(windowsErrorMessage("Serial read failed"));
    }

    if (count > 0) {
        return std::string(buffer, static_cast<std::size_t>(count));
    }

    return {};
#else
    char buffer[256];
    const ssize_t count = read(serialHandle, buffer, sizeof(buffer));
    if (count > 0) {
        return std::string(buffer, static_cast<std::size_t>(count));
    }

    if (count == -1 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        throw std::runtime_error("Serial read failed: " + std::string(std::strerror(errno)));
    }

    return {};
#endif
}

std::optional<Frame> parseFrame(std::string_view line) {
    line = trimLineEnding(line);

    std::istringstream stream { std::string(line) };
    std::string countToken;
    if (!(stream >> countToken)) {
        return std::nullopt;
    }

    Frame frame;
    if (!parseInt(countToken, frame.expectedCount) || frame.expectedCount < 0) {
        return std::nullopt;
    }

    std::string token;
    int slot = 0;
    while (stream >> token) {
        if (slot >= maxInputSlots) {
            break;
        }

        const std::size_t typePos = token.find_first_of("PB");
        if (typePos == std::string::npos || typePos == 0 || typePos + 1 >= token.size()) {
            return std::nullopt;
        }

        int pin = 0;
        int rawValue = 0;
        if (!parseInt(std::string_view(token).substr(0, typePos), pin)
            || !parseInt(std::string_view(token).substr(typePos + 1), rawValue)) {
            return std::nullopt;
        }

        Reading reading;
        reading.slot = slot++;
        reading.pin = pin;
        reading.kind = token[typePos] == 'P' ? Kind::Pot : Kind::Button;
        reading.normalizedValue = reading.kind == Kind::Pot
            ? clamp01(static_cast<float>(rawValue) / 100.0f)
            : (rawValue != 0 ? 1.0f : 0.0f);

        frame.readings.push_back(reading);
    }

    return frame;
}

std::string formatFrame(const Frame& frame) {
    std::ostringstream out;
    out << "data expected=" << frame.expectedCount << " received=" << frame.readings.size();

    for (const auto& reading : frame.readings) {
        out << " | pin " << reading.pin << ' '
            << (reading.kind == Kind::Pot ? "pot" : "button") << '='
            << (reading.kind == Kind::Pot
                ? std::lround(reading.normalizedValue * 100.0f)
                : (reading.normalizedValue >= 0.5f ? 1 : 0));
    }

    return out.str();
}

ParsedLine parseLine(const std::string& line) {
    ParsedLine result;
    const auto frame = parseFrame(line);
    result.isData = frame.has_value();
    if (frame) {
        result.text = formatFrame(*frame);
    } else {
        result.text = line;
    }
    return result;
}

void closeSerialPort(NativeSerialHandle serialHandle) {
#ifdef _WIN32
    CloseHandle(serialHandle);
#else
    close(serialHandle);
#endif
}

}  // namespace hardware
