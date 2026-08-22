#pragma once
#include <cstddef>
#include <memory>

namespace pulsefx {

class PitchShifter {
public:
    PitchShifter();
    ~PitchShifter();

    PitchShifter(const PitchShifter&) = delete;
    PitchShifter& operator=(const PitchShifter&) = delete;

    bool prepare(float sampleRate) noexcept;
    void reset() noexcept;
    void setSemitones(float semitones) noexcept;
    float semitones() const noexcept;
    bool active() const noexcept;
    std::size_t latencySamples() const noexcept;
    // Prepared algorithm latency even while transpose is currently inactive.
    // Used only to reserve latency-matching storage outside the realtime path.
    std::size_t preparedLatencySamples() const noexcept;

    // In-place stereo processing. All scratch storage and spectral state are
    // allocated during prepare(); the realtime path does not allocate.
    void processInterleaved(float* stereo, std::size_t frames) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pulsefx
