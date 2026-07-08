#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <termios.h>
#include <unistd.h>
#include <vector>

#include "hardware.h"

namespace {

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

SerialReader::SerialReader(SerialReader&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

SerialReader& SerialReader::operator=(SerialReader&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }

    return *this;
}

void SerialReader::open(const std::string& device, int baud) {
    close();
    fd_ = openSerialPort(device, baud);
}

void SerialReader::close() {
    if (fd_ != -1) {
        closeSerialPort(fd_);
        fd_ = -1;
    }
}

bool SerialReader::isOpen() const {
    return fd_ != -1;
}

int SerialReader::nativeHandle() const {
    return fd_;
}

std::string SerialReader::readAvailable() const {
    if (fd_ == -1) {
        return {};
    }

    return hardware::readAvailable(fd_);
}

int openSerialPort(const std::string& device, int baud) {
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
}

std::string readAvailable(int serialFd) {
    char buffer[256];
    const ssize_t count = read(serialFd, buffer, sizeof(buffer));
    if (count > 0) {
        return std::string(buffer, static_cast<std::size_t>(count));
    }

    if (count == -1 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        throw std::runtime_error("Serial read failed: " + std::string(std::strerror(errno)));
    }

    return {};
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

void closeSerialPort(int serialFd) {
    close(serialFd);
}

}  // namespace hardware
