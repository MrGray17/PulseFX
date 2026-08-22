#include "pulsefx/VirtualBassEnhancer.h"
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979323846f;

float toneMagnitude(const std::vector<float>& samples, float sampleRate, float frequency) {
    float real = 0.0f;
    float imag = 0.0f;
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const float phase = 2.0f * kPi * frequency * static_cast<float>(index) / sampleRate;
        real += samples[index] * std::cos(phase);
        imag -= samples[index] * std::sin(phase);
    }
    return 2.0f * std::sqrt(real * real + imag * imag) / static_cast<float>(samples.size());
}

void testSilenceRemainsSilence() {
    pulsefx::VirtualBassEnhancer enhancer;
    enhancer.prepare(48000.0f);
    enhancer.setAmount(1.0f);
    enhancer.setBassCapability(0.0f);
    for (int index = 0; index < 4096; ++index) {
        float left = 0.0f;
        float right = 0.0f;
        enhancer.processStereo(left, right);
        assert(left == 0.0f);
        assert(right == 0.0f);
    }
}

void testCapableTransducerIsTransparent() {
    pulsefx::VirtualBassEnhancer enhancer;
    enhancer.prepare(48000.0f);
    enhancer.setAmount(1.0f);
    enhancer.setBassCapability(1.0f);
    for (int index = 0; index < 2048; ++index) {
        const float sourceLeft = 0.25f * std::sin(2.0f * kPi * 80.0f * static_cast<float>(index) / 48000.0f);
        const float sourceRight = sourceLeft * 0.7f;
        float left = sourceLeft;
        float right = sourceRight;
        enhancer.processStereo(left, right);
        assert(std::abs(left - sourceLeft) < 1.0e-7f);
        assert(std::abs(right - sourceRight) < 1.0e-7f);
    }
}

void testLimitedTransducerAddsSecondHarmonic() {
    constexpr float sampleRate = 48000.0f;
    constexpr std::size_t total = 48000;
    constexpr std::size_t warmup = 8000;

    pulsefx::VirtualBassEnhancer enhancer;
    enhancer.prepare(sampleRate);
    enhancer.setAmount(1.0f);
    enhancer.setBassCapability(0.0f);

    std::vector<float> output;
    output.reserve(total - warmup);
    for (std::size_t index = 0; index < total; ++index) {
        const float source = 0.32f * std::sin(2.0f * kPi * 80.0f * static_cast<float>(index) / sampleRate);
        float left = source;
        float right = source;
        enhancer.processStereo(left, right);
        assert(std::isfinite(left));
        assert(std::isfinite(right));
        assert(std::abs(left - right) < 1.0e-7f);
        if (index >= warmup) output.push_back(left);
    }

    const float fundamental = toneMagnitude(output, sampleRate, 80.0f);
    const float second = toneMagnitude(output, sampleRate, 160.0f);
    assert(fundamental > 0.20f);
    assert(second > 0.001f);
}

void testBassRemainsCenteredForAsymmetricInput() {
    pulsefx::VirtualBassEnhancer enhancer;
    enhancer.prepare(48000.0f);
    enhancer.setAmount(0.8f);
    enhancer.setBassCapability(0.0f);

    for (int index = 0; index < 12000; ++index) {
        const float leftSource = 0.25f * std::sin(2.0f * kPi * 72.0f * static_cast<float>(index) / 48000.0f);
        const float rightSource = 0.05f * std::sin(2.0f * kPi * 72.0f * static_cast<float>(index) / 48000.0f);
        float left = leftSource;
        float right = rightSource;
        enhancer.processStereo(left, right);
        const float addedLeft = left - leftSource;
        const float addedRight = right - rightSource;
        assert(std::abs(addedLeft - addedRight) < 1.0e-6f);
    }
}

void testInvalidInputsCannotProduceNonFiniteOutput() {
    pulsefx::VirtualBassEnhancer enhancer;
    enhancer.prepare(48000.0f);
    enhancer.setAmount(std::numeric_limits<float>::infinity());
    enhancer.setBassCapability(std::numeric_limits<float>::quiet_NaN());
    assert(enhancer.amount() == 0.0f);
    assert(enhancer.bassCapability() == 1.0f);

    enhancer.setAmount(1.0f);
    enhancer.setBassCapability(0.0f);
    float left = std::numeric_limits<float>::quiet_NaN();
    float right = std::numeric_limits<float>::infinity();
    enhancer.processStereo(left, right);
    assert(std::isfinite(left));
    assert(std::isfinite(right));
}

} // namespace

int main() {
    testSilenceRemainsSilence();
    testCapableTransducerIsTransparent();
    testLimitedTransducerAddsSecondHarmonic();
    testBassRemainsCenteredForAsymmetricInput();
    testInvalidInputsCannotProduceNonFiniteOutput();
    return 0;
}
