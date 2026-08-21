#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pulsefx::windows {

enum class StereoSampleEncoding {
    Float32,
    Pcm16,
};

inline void decodeStereoSamples(
    const void* source,
    std::size_t frames,
    StereoSampleEncoding encoding,
    float* destination) noexcept {
    if (!destination || frames == 0) return;
    const std::size_t samples = frames * 2;
    if (!source) {
        std::fill_n(destination, samples, 0.0f);
        return;
    }

    if (encoding == StereoSampleEncoding::Float32) {
        std::memcpy(destination, source, samples * sizeof(float));
        return;
    }

    const auto* input = static_cast<const std::int16_t*>(source);
    constexpr float scale = 1.0f / 32768.0f;
    for (std::size_t i = 0; i < samples; ++i) {
        destination[i] = static_cast<float>(input[i]) * scale;
    }
}

} // namespace pulsefx::windows
