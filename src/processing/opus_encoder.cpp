
#include "opus_encoder.h"

#include <algorithm>
#include <cmath>

namespace SashaFM
{
    OpusEncoder::OpusEncoder() = default;

    OpusEncoder::~OpusEncoder()
    {
        reset();
    }

    bool OpusEncoder::initialize(
        int inputSampleRate,
        int channels,
        int bitrate
    )
    {
        reset();

        if (inputSampleRate <= 0 ||
            channels != CHANNELS ||
            bitrate <= 0)
        {
            return false;
        }

        int error = OPUS_OK;

        encoder_ = opus_encoder_create(
            OUTPUT_SAMPLE_RATE,
            CHANNELS,
            OPUS_APPLICATION_AUDIO,
            &error
        );

        if (error != OPUS_OK || encoder_ == nullptr)
        {
            encoder_ = nullptr;
            return false;
        }

        if (opus_encoder_ctl(
            encoder_,
            OPUS_SET_BITRATE(bitrate)
        ) != OPUS_OK)
        {
            reset();
            return false;
        }

        if (opus_encoder_ctl(
            encoder_,
            OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC)
        ) != OPUS_OK)
        {
            reset();
            return false;
        }

        inputSampleRate_ = inputSampleRate;
        resamplePosition_ = 0.0;

        resampleBuffer_.clear();
        frameBuffer_.clear();

        frameBuffer_.reserve(FRAME_SIZE * CHANNELS);

        initialized_ = true;
        return true;
    }

    void OpusEncoder::reset()
    {
        if (encoder_ != nullptr)
        {
            opus_encoder_destroy(encoder_);
            encoder_ = nullptr;
        }

        resampleBuffer_.clear();
        frameBuffer_.clear();

        resamplePosition_ = 0.0;
        initialized_ = false;
    }

    bool OpusEncoder::isInitialized() const
    {
        return initialized_;
    }

    bool OpusEncoder::process(
        const float* samples,
        std::size_t frameCount,
        const PacketCallback& callback
    )
    {
        if (!initialized_ || samples == nullptr || !callback)
            return false;

        if (frameCount == 0)
            return true;

        resampleBuffer_.insert(
            resampleBuffer_.end(),
            samples,
            samples + (frameCount * CHANNELS)
        );

        const std::size_t availableFrames =
            resampleBuffer_.size() / CHANNELS;

        const double step =
            static_cast<double>(inputSampleRate_) /
            OUTPUT_SAMPLE_RATE;

        while (resamplePosition_ + 1.0 < availableFrames)
        {
            const std::size_t index =
                static_cast<std::size_t>(resamplePosition_);

            const double fraction =
                resamplePosition_ - index;

            for (int channel = 0; channel < CHANNELS; ++channel)
            {
                const float a =
                    resampleBuffer_[index * CHANNELS + channel];

                const float b =
                    resampleBuffer_[(index + 1) * CHANNELS + channel];

                const float output =
                    static_cast<float>(a + (b - a) * fraction);

                frameBuffer_.push_back(output);
            }

            resamplePosition_ += step;

            if (frameBuffer_.size() >= FRAME_SIZE * CHANNELS)
            {
                if (!encodeFrame(callback))
                    return false;
            }
        }

        const std::size_t consumedFrames =
            static_cast<std::size_t>(resamplePosition_);

        if (consumedFrames > 0)
        {
            const std::size_t samplesToRemove =
                consumedFrames * CHANNELS;

            resampleBuffer_.erase(
                resampleBuffer_.begin(),
                resampleBuffer_.begin() + samplesToRemove
            );

            resamplePosition_ -= consumedFrames;
        }

        return true;
    }

    bool OpusEncoder::encodeFrame(
        const PacketCallback& callback
    )
    {
        if (frameBuffer_.size() < FRAME_SIZE * CHANNELS)
            return false;

        unsigned char packet[MAX_PACKET_SIZE];

        const int packetSize = opus_encode_float(
            encoder_,
            frameBuffer_.data(),
            FRAME_SIZE,
            packet,
            MAX_PACKET_SIZE
        );

        if (packetSize < 0)
            return false;

        callback(packet, packetSize);

        frameBuffer_.erase(
            frameBuffer_.begin(),
            frameBuffer_.begin() + (FRAME_SIZE * CHANNELS)
        );

        return true;
    }
}
