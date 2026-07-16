// hardware refactor completed on 7/16/2026
// docs comments completed on TBD...

// TODO: create GOOD documentation comments for this file

#include "backend/protocol.h"

#include <charconv>
#include <cmath>
#include <sstream>
#include <string>
#include <system_error>

namespace {

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
    result.text = frame ? formatFrame(*frame) : line;
    return result;
}

}  // namespace hardware
