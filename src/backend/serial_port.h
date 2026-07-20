// hardware refactor completed on 7/16/2026
// docs comments completed on 7/16/2026

#pragma once

#include <string>
#include <string_view>

namespace backend {

#ifdef _WIN32 // ----- WINDOWS TARGET ONLY -----------------------------------------------------------

    // windows serial ports are represented by HANDLE pointers.
    using NativeSerialHandle = void*;
    // nullptr marks an unopened or moved from windows serial handle.
    constexpr NativeSerialHandle invalidSerialHandle = nullptr;

#else // ----- NON-WINDOWS TARGET --------------------------------------------------------------------

    // POSIX serial ports are represented by integer file descriptors.
    using NativeSerialHandle = int;
    // -1 marks an unopened or moved from POSIX serial file descriptor.
    constexpr NativeSerialHandle invalidSerialHandle = -1;

#endif // --------------------------------------------------------------------------------------------

NativeSerialHandle openSerialPort(const std::string& device, int baud);
std::string readAvailable(NativeSerialHandle serialHandle);
void writeSerial(NativeSerialHandle serialHandle, std::string_view message);
void sendSerialLine(NativeSerialHandle serialHandle, std::string_view message);
void closeSerialPort(NativeSerialHandle serialHandle);

}  // namespace backend
