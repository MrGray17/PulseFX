#include "HostProtocol.h"
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    using pulsefx::windows::jsonEscape;
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
    return 0;
}
