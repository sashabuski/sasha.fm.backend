
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <livekit/room.h>
#include <livekit/audio_source.h>
#include <livekit/local_audio_track.h>

namespace SashaFM
{
    class Broadcaster
    {
    public:
        Broadcaster() = default;
        ~Broadcaster();

        Broadcaster(const Broadcaster&) = delete;
        Broadcaster& operator=(const Broadcaster&) = delete;

        bool connectToTokenServer();

        bool connect(
            const std::string& serverUrl,
            const std::string& token
        );

        bool startAudioPublishing();

        // Submit one 10 ms frame of 48 kHz stereo PCM audio.
        bool publishAudioFrame(std::vector<std::int16_t> samples);

        void stopAudioPublishing();
        void disconnect();

        bool isConnected() const noexcept;
        bool isAudioPublished() const noexcept;

    private:
        bool requestToken(
            std::string& serverUrl,
            std::string& token
        );

        std::unique_ptr<livekit::Room> room_;

        std::shared_ptr<livekit::AudioSource> audioSource_;
        std::shared_ptr<livekit::LocalAudioTrack> audioTrack_;

        bool sdkInitialized_ = false;
        bool connected_ = false;
    };
}
