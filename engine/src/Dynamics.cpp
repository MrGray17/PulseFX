#include "pulsefx/Dynamics.h"
#include <algorithm>
#include <cmath>

namespace pulsefx {
namespace {
float dbToLinear(float db) noexcept { return std::pow(10.0f, db / 20.0f); }
float linearToDb(float x) noexcept { return 20.0f * std::log10(std::max(x, 1.0e-9f)); }

float softKneeGainDb(float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept {
    if (ratio <= 1.0f) return 0.0f;
    const float x = inputDb - thresholdDb;
    const float slope = 1.0f / ratio - 1.0f;
    if (x <= -0.5f * kneeDb) return 0.0f;
    if (x >= 0.5f * kneeDb) return slope * x;
    const float y = x + 0.5f * kneeDb;
    return slope * y * y / (2.0f * kneeDb);
}
}

void Dynamics::prepare(float sampleRate) noexcept {
    sampleRate_ = std::clamp(sampleRate, 8000.0f, 384000.0f);
    amount_.prepare(sampleRate_, 45.0f, 0.0f);
    reset();
}

void Dynamics::setAmount(float amount) noexcept {
    amount_.setTarget(std::clamp(amount, 0.0f, 1.0f));
}

void Dynamics::setNightMode(bool enabled) noexcept { nightMode_ = enabled; }

void Dynamics::reset() noexcept {
    detector_ = 0.0f;
    gain_ = 1.0f;
    gainReductionDb_ = 0.0f;
}

void Dynamics::processStereo(float& left, float& right) noexcept {
    const float amount = amount_.next();
    if (amount <= 1.0e-5f && !nightMode_) return;

    const float peak = std::max(std::abs(left), std::abs(right));
    const float attackMs = nightMode_ ? 8.0f : 14.0f;
    const float releaseMs = nightMode_ ? 220.0f : 150.0f;
    const float attackCoeff = std::exp(-1.0f / (0.001f * attackMs * sampleRate_));
    const float releaseCoeff = std::exp(-1.0f / (0.001f * releaseMs * sampleRate_));
    const float coeff = peak > detector_ ? attackCoeff : releaseCoeff;
    detector_ = coeff * detector_ + (1.0f - coeff) * peak;

    const float detectorDb = linearToDb(detector_);
    const float thresholdDb = nightMode_ ? -24.0f : (-10.0f - 8.0f * amount);
    const float ratio = nightMode_ ? 3.5f : (1.0f + 1.4f * amount);
    const float kneeDb = nightMode_ ? 10.0f : 7.0f;
    const float desiredReductionDb = softKneeGainDb(detectorDb, thresholdDb, ratio, kneeDb);
    const float makeupDb = nightMode_ ? 3.0f : 1.3f * amount;
    const float targetGain = dbToLinear(desiredReductionDb + makeupDb);

    const float gainAttackMs = 5.0f;
    const float gainReleaseMs = nightMode_ ? 180.0f : 120.0f;
    const float gainAttack = std::exp(-1.0f / (0.001f * gainAttackMs * sampleRate_));
    const float gainRelease = std::exp(-1.0f / (0.001f * gainReleaseMs * sampleRate_));
    const float gainCoeff = targetGain < gain_ ? gainAttack : gainRelease;
    gain_ = gainCoeff * gain_ + (1.0f - gainCoeff) * targetGain;

    left *= gain_;
    right *= gain_;
    gainReductionDb_ = std::max(0.0f, -linearToDb(std::min(gain_, 1.0f)));
}

} // namespace pulsefx
