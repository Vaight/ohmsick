#include "cli.h"

#include <cerrno>
#include <csignal>
#include <ctime>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <chrono>
#include <thread>
#else
#include <sys/select.h>
#endif

#include "hardware.h"

namespace {

volatile std::sig_atomic_t keepRunning = 1;

struct Options {
    std::string device;
    std::string logPath;
    int baud = 19200;
};

void handleSignal(int) {
    keepRunning = 0;
}

void printUsage(const char* programName) {
    std::cerr
        << "Usage: " << programName << " <serial-device> [--baud <rate>] [--log <file>]\n"
        << "\n"
        << "Examples:\n"
        << "  " << programName << " /dev/ttyACM0\n"
        << "  " << programName << " /dev/ttyUSB0 --log arduino.log\n";
}

std::string timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif

    std::ostringstream out;
    out << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

bool wantsHelp(int argc, char** argv) {
    return argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h");
}

Options parseOptions(int argc, char** argv) {
    if (argc < 2) {
        throw std::runtime_error("missing serial device");
    }

    Options options;
    options.device = argv[1];

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--baud" && i + 1 < argc) {
            options.baud = std::stoi(argv[++i]);
        } else if (arg == "--log" && i + 1 < argc) {
            options.logPath = argv[++i];
        } else {
            throw std::runtime_error("unknown or incomplete option: " + arg);
        }
    }

    return options;
}

void printLine(const std::string& line, std::ofstream& logFile) {
    const hardware::ParsedLine parsed = hardware::parseLine(line);
    const std::string stamp = timestamp();

    std::cout << '[' << stamp << "] " << parsed.text << '\n';

    if (logFile) {
        logFile << '[' << stamp << "] " << line << '\n';
    }
}

void processSerialBuffer(std::string& serialBuffer, std::ofstream& logFile) {
    std::size_t newline = std::string::npos;
    while ((newline = serialBuffer.find('\n')) != std::string::npos) {
        std::string line = serialBuffer.substr(0, newline);
        serialBuffer.erase(0, newline + 1);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        printLine(line, logFile);
    }
}

}  // namespace

namespace cli {

int run(int argc, char** argv) {
    if (wantsHelp(argc, argv)) {
        printUsage(argv[0]);
        return 0;
    }

    Options options;
    try {
        options = parseOptions(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n\n";
        printUsage(argv[0]);
        return 1;
    }

    std::ofstream logFile;
    if (!options.logPath.empty()) {
        logFile.open(options.logPath, std::ios::app);
        if (!logFile) {
            std::cerr << "Could not open log file: " << options.logPath << '\n';
            return 1;
        }
    }

    std::signal(SIGINT, handleSignal);
#ifndef _WIN32
    std::signal(SIGTERM, handleSignal);
#endif

    hardware::NativeSerialHandle serialHandle = hardware::invalidSerialHandle;
    try {
        serialHandle = hardware::openSerialPort(options.device, options.baud);
        std::cout << "Listening on " << options.device << " at " << options.baud << " baud. Press Ctrl+C to quit.\n";

        std::string serialBuffer;

        while (keepRunning) {
#ifdef _WIN32
            serialBuffer += hardware::readAvailable(serialHandle);
            processSerialBuffer(serialBuffer, logFile);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
#else
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(serialHandle, &readSet);

            const int ready = select(serialHandle + 1, &readSet, nullptr, nullptr, nullptr);
            if (ready == -1) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error("select failed: " + std::string(std::strerror(errno)));
            }

            if (FD_ISSET(serialHandle, &readSet)) {
                serialBuffer += hardware::readAvailable(serialHandle);
                processSerialBuffer(serialBuffer, logFile);
            }
#endif
        }

        hardware::closeSerialPort(serialHandle);
        serialHandle = hardware::invalidSerialHandle;
    } catch (const std::exception& error) {
        if (serialHandle != hardware::invalidSerialHandle) {
            hardware::closeSerialPort(serialHandle);
        }
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }

    std::cout << "\nStopped.\n";
    return 0;
}

}  // namespace cli

int main(int argc, char** argv) {
    return cli::run(argc, argv);
}
