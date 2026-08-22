#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace pulsefx::windows {

struct HostCommand {
    std::string name;
    std::vector<std::string> args;
};

HostCommand parseHostCommand(std::string_view line);
std::string jsonEscape(std::string_view value);

} // namespace pulsefx::windows
