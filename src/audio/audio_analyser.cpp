
#include "audio_analyser.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace SashaFM
{
    namespace
    {
        constexpr float SAMPLE_RATE = 48000.0f;
        constexpr float MIN_FREQUENCY = 40.0f;
        constexpr float MAX_FREQUENCY = 16000.0f;

        constexpr float PI = std::numbers::pi_v<float>;

        using Complex = std::complex<float>;
    }

    AudioAnalyser::AudioAnalyser()
    {
        reset();
    }

    void AudioAnalyser::reset()
    {
        sampleBuffer.fill(0.0f);
        bands.fill(0.0f);

        writePosition = 0;
        samplesCollected = 0;
    }

    void AudioAnalyser::addSamples(
        const float* samples,
        std::size_t sampleCount
    )
    {
        if (!samples || sampleCount < 2)
            return;

        // Input samples are interleaved stereo:
        // Left, Right, Left, Right...
        for (std::size_t i = 0; i + 1 < sampleCount; i += 2)
        {
            // Convert stereo to mono for frequency analysis.
            const float mono =
                (samples[i] + samples[i + 1]) * 0.5f;

            sampleBuffer[writePosition] = mono;

            writePosition =
                (writePosition + 1) % FFT_SIZE;

            if (samplesCollected < FFT_SIZE)
                ++samplesCollected;

            if (samplesCollected == FFT_SIZE &&
                writePosition == 0)
            {
                analyse();
            }
        }
    }

    AudioAnalyser::Bands AudioAnalyser::getBands() const
    {
        return bands;
    }

    void AudioAnalyser::analyse()
    {
        std::array<Complex, FFT_SIZE> fft{};

        // Apply Hann window.
        for (std::size_t i = 0; i < FFT_SIZE; ++i)
        {
            const float window =
                0.5f * (1.0f - std::cos(
                    2.0f * PI * static_cast<float>(i) /
                    static_cast<float>(FFT_SIZE - 1)
                ));

            const std::size_t index =
                (writePosition + i) % FFT_SIZE;

            fft[i] = Complex(
                sampleBuffer[index] * window,
                0.0f
            );
        }

        // Bit-reversal permutation.
        std::size_t j = 0;

        for (std::size_t i = 1; i < FFT_SIZE; ++i)
        {
            std::size_t bit = FFT_SIZE >> 1;

            while (j & bit)
            {
                j ^= bit;
                bit >>= 1;
            }

            j ^= bit;

            if (i < j)
                std::swap(fft[i], fft[j]);
        }

        // Iterative radix-2 FFT.
        for (std::size_t length = 2;
             length <= FFT_SIZE;
             length <<= 1)
        {
            const float angle =
                -2.0f * PI / static_cast<float>(length);

            const Complex root(
                std::cos(angle),
                std::sin(angle)
            );

            for (std::size_t i = 0; i < FFT_SIZE; i += length)
            {
                Complex factor(1.0f, 0.0f);

                for (std::size_t k = 0; k < length / 2; ++k)
                {
                    const Complex even = fft[i + k];
                    const Complex odd =
                        fft[i + k + length / 2] * factor;

                    fft[i + k] = even + odd;
                    fft[i + k + length / 2] = even - odd;

                    factor *= root;
                }
            }
        }

        // Convert FFT bins into 32 logarithmic frequency bands.
        for (std::size_t band = 0; band < BAND_COUNT; ++band)
        {
            const float lowerFrequency =
                MIN_FREQUENCY * std::pow(
                    MAX_FREQUENCY / MIN_FREQUENCY,
                    static_cast<float>(band) / BAND_COUNT
                );

            const float upperFrequency =
                MIN_FREQUENCY * std::pow(
                    MAX_FREQUENCY / MIN_FREQUENCY,
                    static_cast<float>(band + 1) / BAND_COUNT
                );

            const std::size_t firstBin =
                std::max<std::size_t>(
                    1,
                    static_cast<std::size_t>(
                        lowerFrequency * FFT_SIZE / SAMPLE_RATE
                    )
                );

            const std::size_t lastBin =
                std::min<std::size_t>(
                    FFT_SIZE / 2,
                    static_cast<std::size_t>(
                        std::ceil(
                            upperFrequency * FFT_SIZE / SAMPLE_RATE
                        )
                    )
                );

            float maximumMagnitude = 0.0f;

            for (std::size_t bin = firstBin;
                 bin <= lastBin && bin < FFT_SIZE / 2;
                 ++bin)
            {
                const float magnitude =
                    std::abs(fft[bin]) * (2.0f / FFT_SIZE);

                maximumMagnitude =
                    (std::max)(maximumMagnitude, magnitude);
            }

            // Apply gain and clamp to a normalised 0-1 range.
            const float level =
                std::clamp(maximumMagnitude * 8.0f, 0.0f, 1.0f);

            bands[band] = level;
        }
    }
}
