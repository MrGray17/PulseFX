#include "HostProtocol.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    using pulsefx::windows::jsonEscape;
    using pulsefx::windows::parseFiniteFloat;
    using pulsefx::windows::parseHostCommand;

    const auto simple = parseHostCommand("eq 17 3.5");
    require(simple.name == "eq", "command name parse failed");
    require(simple.args.size() == 2 && simple.args[0] == "17" && simple.args[1] == "3.5", "command args parse failed");

    const auto quoted = parseHostCommand("output \"device id with spaces\"");
    require(quoted.name == "output", "quoted command name parse failed");
    require(quoted.args.size() == 1 && quoted.args[0] == "device id with spaces", "quoted arg parse failed");

    const auto escaped = parseHostCommand("output \"a\\\"b\\\\c\"");
    require(escaped.args.size() == 1 && escaped.args[0] == "a\"b\\c", "quoted escape parse failed");

    require(jsonEscape("a\"b\\c\n") == "a\\\"b\\\\c\\n", "JSON escaping failed");
    require(jsonEscape(std::string_view("\x01", 1)) == "\\u0001", "JSON control escaping failed");

    float value = 0.0f;
    require(parseFiniteFloat("3.5", value) && std::abs(value - 3.5f) < 1.0e-6f, "finite float parse failed");
    require(parseFiniteFloat("-1.25e-2", value) && std::abs(value + 0.0125f) < 1.0e-6f, "scientific float parse failed");
    require(!parseFiniteFloat("nan", value), "NaN was accepted by native control parser");
    require(!parseFiniteFloat("NaN", value), "case-variant NaN was accepted by native control parser");
    require(!parseFiniteFloat("inf", value), "infinity was accepted by native control parser");
    require(!parseFiniteFloat("-inf", value), "negative infinity was accepted by native control parser");
    require(!parseFiniteFloat("1.0junk", value), "partial numeric token was accepted");
    require(!parseFiniteFloat("1e9999", value), "overflowing numeric token was accepted");
    require(!parseFiniteFloat("", value), "empty numeric token was accepted");
    return 0;
}
