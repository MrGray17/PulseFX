#include "HostProtocol.h"
#include <cctype>

namespace pulsefx::windows {

HostCommand parseHostCommand(std::string_view line) {
    HostCommand command{};
    std::size_t index = 0;

    const auto skipSpaces = [&]() {
        while (index < line.size() && std::isspace(static_cast<unsigned char>(line[index])) != 0) ++index;
    };

    const auto readToken = [&]() -> std::string {
        skipSpaces();
        if (index >= line.size()) return {};
        std::string token;
        if (line[index] == '"') {
            ++index;
            while (index < line.size()) {
                const char c = line[index++];
                if (c == '"') break;
                if (c == '\\' && index < line.size()) {
                    const char escaped = line[index++];
                    switch (escaped) {
                    case 'n': token.push_back('\n'); break;
                    case 'r': token.push_back('\r'); break;
                    case 't': token.push_back('\t'); break;
                    case '"': token.push_back('"'); break;
                    case '\\': token.push_back('\\'); break;
                    default: token.push_back(escaped); break;
                    }
                } else {
                    token.push_back(c);
                }
            }
            return token;
        }
        const std::size_t begin = index;
        while (index < line.size() && std::isspace(static_cast<unsigned char>(line[index])) == 0) ++index;
        return std::string(line.substr(begin, index - begin));
    };

    command.name = readToken();
    while (true) {
        const std::string token = readToken();
        if (token.empty()) break;
        command.args.push_back(token);
    }
    return command;
}

std::string jsonEscape(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 8);
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char c : value) {
        switch (c) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (c < 0x20) {
                result += "\\u00";
                result.push_back(hex[(c >> 4) & 0x0f]);
                result.push_back(hex[c & 0x0f]);
            } else {
                result.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    return result;
}

} // namespace pulsefx::windows
