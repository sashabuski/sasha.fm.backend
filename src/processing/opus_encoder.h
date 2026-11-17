#pragma once

#include <opus/opus.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace SashaFM
{
    class OpusEncoder
    {
    public:
        using PacketCallback =
            std::function<void(const unsigned char*, int)>;

        OpusEncoder();
        ~OpusEncoder();

        OpusEncoder(const OpusEncoder&) = delete;
        OpusEncoder& operator=(const OpusEncoder&) = delete;

        bool initialize(
            int inputSampleRate = 44100,
            int channels = 2,
            int bitrate = 128000
        );

        void reset();

        bool process(
            const float* samples,
            std::size_t frameCount,
            const PacketCallback& callback
        );

        bool isInitialized() const;

    private:
        bool encodeFrame(const PacketCallback& callback);

        static constexpr int OUTPUT_SAMPLE_RATE = 48000;
        static constexpr int CHANNELS = 2;
        static constexpr int FRAME_SIZE = 960;
        static constexpr int MAX_PACKET_SIZE = 4000;

        ::OpusEncoder* encoder_ = nullptr;

        int inputSampleRate_ = 44100;
        double resamplePosition_ = 0.0;

        std::vector<float> resampleBuffer_;
        std::vector<float> frameBuffer_;

        bool initialized_ = false;
    };
}