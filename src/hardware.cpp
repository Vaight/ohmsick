#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sstream>
#include <stdexcept>
#include <string>
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

bool parseDataLine(const std::string& line, std::string& parsed) {
    std::istringstream stream(line);
    int expectedCount = 0;
    if (!(stream >> expectedCount)) {
        return false;
    }

    std::vector<std::string> readings;
    std::string token;
    while (stream >> token) {
        if (token.size() < 3) {
            return false;
        }

        const std::size_t typePos = token.find_first_of("PB");
        if (typePos == std::string::npos || typePos == 0 || typePos + 1 >= token.size()) {
            return false;
        }

        const std::string pin = token.substr(0, typePos);
        const char kind = token[typePos];
        const std::string value = token.substr(typePos + 1);

        std::ostringstream reading;
        reading << "pin " << pin << ' ' << (kind == 'P' ? "pot" : "button") << '=' << value;
        readings.push_back(reading.str());
    }

    std::ostringstream out;
    out << "data expected=" << expectedCount << " received=" << readings.size();
    for (const auto& reading : readings) {
        out << " | " << reading;
    }

    parsed = out.str();
    return true;
}

}  // namespace

namespace hardware {

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

ParsedLine parseLine(const std::string& line) {
    ParsedLine result;
    result.isData = parseDataLine(line, result.text);
    if (!result.isData) {
        result.text = line;
    }
    return result;
}

void closeSerialPort(int serialFd) {
    close(serialFd);
}

}  // namespace hardware
