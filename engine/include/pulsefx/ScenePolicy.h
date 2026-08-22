#pragma once
#include "AdaptiveSignature.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace pulsefx {

struct SceneRule {
    std::string processKey;
    ContentClass content{ContentClass::General};
    bool lowLatency{false};
    int priority{50};
};

struct SceneSession {
    std::string processKey;
    std::uint32_t processId{0};
    bool active{false};
    bool muted{false};
    float volume{1.0f};
};

struct SceneSelection {
    bool matched{false};
    std::string processKey;
    std::uint32_t processId{0};
    ContentClass content{ContentClass::General};
    bool lowLatency{false};
    int priority{0};
};

inline std::string normalizeSceneProcessKey(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    const auto last = value.find_last_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    value = value.substr(first, last - first + 1);
    const auto slash = value.find_last_of("\\/");
    if (slash != std::string::npos) value = value.substr(slash + 1);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (value.size() > 260) value.resize(260);
    return value;
}

inline SceneSelection chooseScene(
    const std::vector<SceneRule>& rules,
    const std::vector<SceneSession>& sessions) {
    SceneSelection best{};
    float bestVolume = -1.0f;

    for (const auto& session : sessions) {
        if (!session.active || session.muted) continue;
        const float volume = std::clamp(
            std::isfinite(session.volume) ? session.volume : 0.0f,
            0.0f,
            1.0f);
        if (volume <= 0.001f) continue;
        const auto processKey = normalizeSceneProcessKey(session.processKey);
        if (processKey.empty()) continue;

        for (const auto& rule : rules) {
            if (normalizeSceneProcessKey(rule.processKey) != processKey) continue;
            const int priority = std::clamp(rule.priority, 0, 100);
            const bool better = !best.matched ||
                priority > best.priority ||
                (priority == best.priority && volume > bestVolume) ||
                (priority == best.priority && volume == bestVolume && session.processId < best.processId);
            if (!better) continue;
            best.matched = true;
            best.processKey = processKey;
            best.processId = session.processId;
            best.content = rule.content;
            best.lowLatency = rule.lowLatency;
            best.priority = priority;
            bestVolume = volume;
        }
    }
    return best;
}

} // namespace pulsefx
