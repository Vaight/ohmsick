// hardware refactor completed on 7/16/2026
// docs comments completed on 7/16/2026

#include "backend/serial_port.h"

#include <algorithm>
#include <cerrno>
#include <stdexcept>
#include <string>

// windows-only definitions and includes
#ifdef _WIN32 // ----- WINDOWS TARGET ONLY -----------------------------------------------------------

    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>

#else // ----- NON-WINDOWS TARGET --------------------------------------------------------------------

    #include <cstring>
    #include <fcntl.h>
    #include <termios.h>
    #include <unistd.h>

#endif // --------------------------------------------------------------------------------------------

namespace {

#ifdef _WIN32 // ----- WINDOWS TARGET ONLY -----------------------------------------------------------

    /*
     * convert a supported baud rate integer to the DWORD value used by win32 serial apis.
     * PARAMS:
     *   ∟ int baud    : requested baud rate
     * RETURNS:
     *   ∟ DWORD       : baud rate value accepted by DCB::BaudRate
     */
    DWORD baudToSpeed(int baud) {
        // pass through the supported baud rates as DWORD values.
        switch (baud) {
            case 9600:
            case 19200:
            case 38400:
            case 57600:
            case 115200:
                return static_cast<DWORD>(baud);
            default:
                // reject rates that this backend does not configure.
                throw std::runtime_error("Unsupported baud rate. Try 9600, 19200, 38400, 57600, or 115200.");
        }
    }

    /*
     * build an exception-ready message from the latest win32 error code.
     * PARAMS:
     *   ∟ const std::string &prefix   : operation-specific message prefix
     * RETURNS:
     *   ∟ std::string                 : prefix plus formatted system error text
     */
    std::string windowsErrorMessage(const std::string &prefix) {
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

    /*
     * normalize a windows serial device name to the path form required by CreateFileA.
     * PARAMS:
     *   ∟ const std::string &device   : serial name such as "COM3" or "\\.\COM3"
     * RETURNS:
     *   ∟ std::string                 : path form such as "\\.\COM3"
     */
    std::string normalizeWindowsDeviceName(const std::string &device) {
        // keep device names that already use the win32 device namespace.
        if (device.rfind("\\\\.\\", 0) == 0) return device;
        // add the namespace prefix required for serial ports such as COM10 and above.
        return "\\\\.\\" + device;
    }

#else // ----- NON-WINDOWS TARGET --------------------------------------------------------------------

    /*
     * convert a supported baud rate integer to the speed_t value used by termios.
     * PARAMS:
     *   ∟ int baud    : requested baud rate
     * RETURNS:
     *   ∟ speed_t     : termios baud constant
     */
    speed_t baudToSpeed(int baud) {
        // map each supported baud rate to its platform constant.
        switch (baud) {
            case 9600: return B9600;
            case 19200: return B19200;
            case 38400: return B38400;
            case 57600: return B57600;
            case 115200: return B115200;
            default:
                // reject rates that this backend does not configure.
                throw std::runtime_error("Unsupported baud rate. Try 9600, 19200, 38400, 57600, or 115200.");
        }
    }

#endif // --------------------------------------------------------------------------------------------

}  // namespace

namespace backend {

/*
 * open and configure a native serial port for nonblocking read/write access.
 * PARAMS:
 *   ∟ const std::string &device   : serial device path or name, such as "/dev/ttyUSB0" or "COM3"
 *   ∟ int baud                    : baud rate to configure
 * RETURNS:
 *   ∟ NativeSerialHandle          : platform-specific open serial handle
 */
NativeSerialHandle openSerialPort(const std::string &device, int baud) {

    #ifdef _WIN32 // ----- WINDOWS TARGET ONLY -----------------------------------------------------------

        // convert shorthand names like COM3 to the win32 device namespace form.
        const std::string normalizedDevice = normalizeWindowsDeviceName(device);

        // open the serial device for synchronous read/write access.
        HANDLE handle = CreateFileA(
            normalizedDevice.c_str(),        // device path to open
            GENERIC_READ | GENERIC_WRITE,    // allow both receiving and transmitting bytes
            0,                               // do not share the serial port with other processes
            nullptr,                         // use default security attributes
            OPEN_EXISTING,                   // serial devices must already exist
            FILE_ATTRIBUTE_NORMAL,           // use normal synchronous file behavior
            nullptr                          // no template file is used
        );

        // report the formatted win32 error if the port could not be opened.
        if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error(windowsErrorMessage("Could not open " + device));

        // load the current device control settings before changing serial fields.
        DCB dcb {};
        dcb.DCBlength = sizeof(dcb);
        
        // read the existing serial configuration into the DCB structure.
        if (!GetCommState(handle, &dcb)) {
            CloseHandle(handle);
            throw std::runtime_error(windowsErrorMessage("Could not read serial settings"));
        }

        // configure an 8-n-1 serial connection with control lines enabled.
        dcb.BaudRate = baudToSpeed(baud);       // set the requested baud rate
        dcb.ByteSize = 8;                       // use 8 data bits per byte
        dcb.Parity = NOPARITY;                  // disable parity checking
        dcb.StopBits = ONESTOPBIT;              // use one stop bit
        dcb.fBinary = TRUE;                     // enable binary mode for all transferred bytes
        dcb.fDtrControl = DTR_CONTROL_ENABLE;   // assert dtr for devices that rely on it
        dcb.fRtsControl = RTS_CONTROL_ENABLE;   // assert rts for devices that rely on it

        // apply the updated DCB settings to the open serial handle.
        if (!SetCommState(handle, &dcb)) {
            CloseHandle(handle);
            throw std::runtime_error(windowsErrorMessage("Could not apply serial settings"));
        }

        // configure nonblocking-style reads that return immediately with available data.
        COMMTIMEOUTS timeouts {};
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.ReadTotalTimeoutConstant = 0;

        // apply read timeout behavior to the open serial handle.
        if (!SetCommTimeouts(handle, &timeouts)) {
            CloseHandle(handle);
            throw std::runtime_error(windowsErrorMessage("Could not apply serial timeouts"));
        }

        // discard stale receive and transmit bytes from before this connection.
        PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);

        return handle;

    #else // ----- NON-WINDOWS TARGET --------------------------------------------------------------------

        // open the serial device for read/write access without becoming the controlling terminal.
        const int fd = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);

        // report the errno message when the device cannot be opened.
        if (fd == -1) throw std::runtime_error("Could not open " + device + ": " + std::strerror(errno));

        // load the current terminal settings before changing serial fields.
        termios tty {};

        // read the existing serial configuration into the termios structure.
        if (tcgetattr(fd, &tty) != 0) {
            ::close(fd);
            throw std::runtime_error("Could not read serial settings: " + std::string(std::strerror(errno)));
        }

        // disable terminal processing and set matching input/output baud rates.
        cfmakeraw(&tty);                        // pass bytes through without canonical or echo processing
        cfsetispeed(&tty, baudToSpeed(baud));   // set receive baud rate
        cfsetospeed(&tty, baudToSpeed(baud));   // set transmit baud rate

        // configure an 8-n-1 serial connection without hardware flow control.
        tty.c_cflag |= CLOCAL | CREAD;    // ignore modem control lines and enable the receiver
        tty.c_cflag &= ~CSIZE;            // clear the current character-size bits
        tty.c_cflag |= CS8;               // use 8 data bits per byte
        tty.c_cflag &= ~PARENB;           // disable parity checking
        tty.c_cflag &= ~CSTOPB;           // use one stop bit
        tty.c_cflag &= ~CRTSCTS;          // disable rts/cts hardware flow control

        // allow reads to return with no bytes, waiting up to 0.1 seconds for the first byte.
        tty.c_cc[VMIN] = 0;
        tty.c_cc[VTIME] = 1;

        // apply the termios settings immediately.
        if (tcsetattr(fd, TCSANOW, &tty) != 0) {
            ::close(fd);
            throw std::runtime_error("Could not apply serial settings: " + std::string(std::strerror(errno)));
        }

        // discard stale input and output bytes from before this connection.
        tcflush(fd, TCIOFLUSH);
        return fd;

    #endif // --------------------------------------------------------------------------------------------
}

/*
 * read any bytes currently available from an open serial handle.
 * PARAMS:
 *   ∟ NativeSerialHandle serialHandle   : open platform-specific serial handle
 * RETURNS:
 *   ∟ std::string                       : bytes read, or empty when no data is available
 */
std::string readAvailable(NativeSerialHandle serialHandle) {

    #ifdef _WIN32 // ----- WINDOWS TARGET ONLY -----------------------------------------------------------

        // stack buffer for one read attempt.
        char buffer[256];

        // receives the number of bytes copied into the buffer.
        DWORD count = 0;

        // read whatever bytes the port can provide under the configured timeouts.
        if (!ReadFile(serialHandle, buffer, sizeof(buffer), &count, nullptr)) throw std::runtime_error(windowsErrorMessage("Serial read failed"));

        // return only the bytes actually read.
        if (count > 0) return std::string(buffer, static_cast<std::size_t>(count));

        // no bytes were available before the read returned.
        return {};

    #else // ----- NON-WINDOWS TARGET --------------------------------------------------------------------

        // stack buffer for one read attempt.
        char buffer[256];

        // read from the nonblocking file descriptor.
        const ssize_t count = ::read(serialHandle, buffer, sizeof(buffer));

        // return only the bytes actually read.
        if (count > 0) return std::string(buffer, static_cast<std::size_t>(count));

        // ignore nonblocking and interrupted-read cases; throw other read failures.
        if (count == -1 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) throw std::runtime_error("Serial read failed: " + std::string(std::strerror(errno)));

        // no bytes were available before the read returned.
        return {};

    #endif // --------------------------------------------------------------------------------------------

}

/*
 * write the complete message to an open serial handle.
 * PARAMS:
 *   ∟ NativeSerialHandle serialHandle   : open platform-specific serial handle
 *   ∟ std::string_view message          : bytes to transmit
 * RETURNS:
 *   ∟ void
 */
void writeSerial(NativeSerialHandle serialHandle, std::string_view message) {

    #ifdef _WIN32 // ----- WINDOWS TARGET ONLY -----------------------------------------------------------

        // point to the next byte that still needs to be written.
        const char* cursor = message.data();
        // track how many bytes remain after partial writes.
        std::size_t remaining = message.size();

        // continue until every byte in the message has been accepted.
        while (remaining > 0) {

            // receives the number of bytes written by this WriteFile call.
            DWORD written = 0;
            // limit each write request to a size accepted by the DWORD api.
            const DWORD chunkSize = static_cast<DWORD>(std::min<std::size_t>(remaining, 4096));

            // write the current chunk to the serial handle.
            if (!WriteFile(serialHandle, cursor, chunkSize, &written, nullptr)) throw std::runtime_error(windowsErrorMessage("Serial write failed"));

            // guard against an infinite loop if the operating system accepts no bytes.
            if (written == 0) throw std::runtime_error("Serial write failed: wrote zero bytes");

            cursor += written;
            remaining -= written;
        }

    #else // ----- NON-WINDOWS TARGET --------------------------------------------------------------------

        // point to the next byte that still needs to be written.
        const char* cursor = message.data();
        // track how many bytes remain after partial writes.
        std::size_t remaining = message.size();

        // continue until every byte in the message has been accepted.
        while (remaining > 0) {

            // attempt to write the remaining bytes to the file descriptor.
            const ssize_t written = ::write(serialHandle, cursor, remaining);

            // retry interrupted or temporarily blocked writes; throw other failures.
            if (written == -1) {
                if (errno == EINTR) {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                throw std::runtime_error("Serial write failed: " + std::string(std::strerror(errno)));
            }

            // guard against an infinite loop if the operating system accepts no bytes.
            if (written == 0) throw std::runtime_error("Serial write failed: wrote zero bytes");

            cursor += written;
            // reduce the remaining byte count by the successful partial write.
            remaining -= static_cast<std::size_t>(written);
        }

    #endif // --------------------------------------------------------------------------------------------

}

/*
 * write a message followed by a newline to an open serial handle.
 * PARAMS:
 *   ∟ NativeSerialHandle serialHandle   : open platform-specific serial handle
 *   ∟ std::string_view message          : line content to transmit
 * RETURNS:
 *   ∟ void
 */
void sendSerialLine(NativeSerialHandle serialHandle, std::string_view message) {
    writeSerial(serialHandle, message);
    writeSerial(serialHandle, "\n");
}

/*
 * close a platform-specific serial handle.
 * PARAMS:
 *   ∟ NativeSerialHandle serialHandle   : open serial handle to close
 * RETURNS:
 *   ∟ void
 */
void closeSerialPort(NativeSerialHandle serialHandle) {
    #ifdef _WIN32 // ----- WINDOWS TARGET ONLY -----------------------------------------------------------

        CloseHandle(serialHandle);

    #else // ----- NON-WINDOWS TARGET --------------------------------------------------------------------

        ::close(serialHandle);

    #endif // --------------------------------------------------------------------------------------------
}

}  // end namespace
