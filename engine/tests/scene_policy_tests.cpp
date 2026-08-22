#include "pulsefx/ScenePolicy.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <vector>

using namespace pulsefx;

int main() {
    assert(normalizeSceneProcessKey("  C:\\Apps\\SPOTIFY.EXE  ") == "spotify.exe");
    assert(normalizeSceneProcessKey("discord.exe") == "discord.exe");
    assert(normalizeSceneProcessKey("   ").empty());

    std::vector<SceneRule> rules{
        {"spotify.exe", ContentClass::Music, false, 60},
        {"game.exe", ContentClass::Game, true, 90},
        {"discord.exe", ContentClass::Voice, true, 80},
    };

    std::vector<SceneSession> sessions{
        {"spotify.exe", 10, true, false, 0.8f},
        {"game.exe", 20, false, false, 1.0f},
    };
    auto selected = chooseScene(rules, sessions);
    assert(selected.matched);
    assert(selected.processKey == "spotify.exe");
    assert(selected.content == ContentClass::Music);
    assert(!selected.lowLatency);

    sessions[1].active = true;
    selected = chooseScene(rules, sessions);
    assert(selected.processKey == "game.exe");
    assert(selected.content == ContentClass::Game);
    assert(selected.lowLatency);

    sessions[1].muted = true;
    selected = chooseScene(rules, sessions);
    assert(selected.processKey == "spotify.exe");
    sessions[1].muted = false;
    sessions[1].volume = 0.0f;
    selected = chooseScene(rules, sessions);
    assert(selected.processKey == "spotify.exe");

    rules.push_back({"browser.exe", ContentClass::Movie, false, 60});
    sessions.push_back({"browser.exe", 30, true, false, 0.9f});
    selected = chooseScene(rules, sessions);
    assert(selected.processKey == "browser.exe");
    sessions[2].volume = 0.8f;
    selected = chooseScene(rules, sessions);
    assert(selected.processKey == "spotify.exe");

    sessions[0].volume = std::numeric_limits<float>::quiet_NaN();
    selected = chooseScene(rules, sessions);
    assert(selected.processKey == "browser.exe");

    std::vector<SceneSession> none{{"unknown.exe", 99, true, false, 1.0f}};
    selected = chooseScene(rules, none);
    assert(!selected.matched);
    assert(selected.content == ContentClass::General);

    // Signature Strength is part of the same high-level policy surface. It must
    // scale optional enhancement monotonically while staying inside all hard
    // audio/headroom bounds and preserving harshness protection.
    SignatureInputs base{};
    base.knowledge = DeviceKnowledge::Measured;
    base.content = ContentClass::Music;
    base.lowFrequencyCapability = 0.35f;
    base.harshnessRisk = 0.15f;
    SignatureInputs subtle = base;
    subtle.strength = 0.50f;
    SignatureInputs expansive = base;
    expansive.strength = 1.25f;
    const auto low = makeAdaptiveSignature(subtle);
    const auto normal = makeAdaptiveSignature(base);
    const auto high = makeAdaptiveSignature(expansive);
    assert(low.physicalBass <= normal.physicalBass);
    assert(low.clarity <= normal.clarity);
    assert(low.surround <= normal.surround);
    assert(high.physicalBass >= normal.physicalBass);
    assert(high.clarity >= normal.clarity);
    assert(high.surround >= normal.surround);
    assert(high.preampDb <= normal.preampDb);

    SignatureInputs harsh = expansive;
    harsh.harshnessRisk = 1.0f;
    const auto harshPlan = makeAdaptiveSignature(harsh);
    assert(harshPlan.clarity < high.clarity);
    assert(harshPlan.fidelity < high.fidelity);
    assert(harshPlan.preampDb >= -9.0f && harshPlan.preampDb <= -0.5f);
    assert(harshPlan.physicalBass >= 0.0f && harshPlan.physicalBass <= 0.42f);
    assert(harshPlan.virtualBass >= 0.0f && harshPlan.virtualBass <= 0.55f);
    assert(harshPlan.surround >= 0.0f && harshPlan.surround <= 0.62f);

    SignatureInputs hostile = base;
    hostile.strength = std::numeric_limits<float>::quiet_NaN();
    const auto safe = makeAdaptiveSignature(hostile);
    assert(std::isfinite(safe.preampDb));
    assert(std::isfinite(safe.surround));
    return 0;
}
