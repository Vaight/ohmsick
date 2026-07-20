// hardware refactor completed on 7/16/2026
// docs comments completed on TBD...

// TODO: create GOOD documentation comments for this file

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace backend {

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

std::optional<Frame> parseFrame(std::string_view line);
std::string formatFrame(const Frame& frame);
ParsedLine parseLine(const std::string& line);

}  // namespace backend
