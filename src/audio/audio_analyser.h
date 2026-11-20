
#pragma once

#include <array>
#include <vector>
#include <cstddef>

namespace SashaFM
{
    class AudioAnalyser
    {
    public:
        static constexpr std::size_t BAND_COUNT = 32;
        static constexpr std::size_t FFT_SIZE = 2048;

        using Bands = std::array<float, BAND_COUNT>;

        AudioAnalyser();

        // Add stereo audio samples for analysis.
        void addSamples(
            const float* samples,
            std::size_t sampleCount
        );

        // Retrieve the latest 32 frequency levels.
        Bands getBands() const;

        void reset();

    private:
        void analyse();

        std::array<float, FFT_SIZE> sampleBuffer{};
        std::size_t writePosition = 0;
        std::size_t samplesCollected = 0;

        Bands bands{};
    };
}
