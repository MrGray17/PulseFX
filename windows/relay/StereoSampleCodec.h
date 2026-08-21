#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pulsefx::windows {

enum class SampleEncoding {
    Float32,
    Pcm16,
};

// Backward-compatible alias for older relay tests/callers.
using StereoSampleEncoding = SampleEncoding;

inline void decodeInterleavedSamples(
    const void* source,
    std::size_t frames,
    std::size_t channels,
    SampleEncoding encoding,
    float* destination) noexcept {
    if (!destination || frames == 0 || channels == 0) return;
    const std::size_t samples = frames * channels;
    if (!source) {
        std::fill_n(destination, samples, 0.0f);
        return;
    }

    if (encoding == SampleEncoding::Float32) {
        std::memcpy(destination, source, samples * sizeof(float));
        return;
    }

    const auto* input = static_cast<const std::int16_t*>(source);
    constexpr float scale = 1.0f / 32768.0f;
    for (std::size_t i = 0; i < samples; ++i) {
        destination[i] = static_cast<float>(input[i]) * scale;
    }
}

inline void decodeStereoSamples(
    const void* source,
    std::size_t frames,
    StereoSampleEncoding encoding,
    float* destination) noexcept {
    decodeInterleavedSamples(source, frames, 2, encoding, destination);
}

} // namespace pulsefx::windows
