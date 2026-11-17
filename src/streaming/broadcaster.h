
#pragma once

#include <memory>
#include <string>

#include <livekit/room.h>

namespace SashaFM
{
    class Broadcaster
    {
    public:
        Broadcaster() = default;
        ~Broadcaster();

        Broadcaster(const Broadcaster&) = delete;
        Broadcaster& operator=(const Broadcaster&) = delete;

        // Request a broadcaster token and connect to LiveKit.
        bool connectToTokenServer();

        // Connect using an already obtained URL and token.
        bool connect(
            const std::string& serverUrl,
            const std::string& token
        );

        void disconnect();

        bool isConnected() const noexcept;

    private:
        bool requestToken(
            std::string& serverUrl,
            std::string& token
        );

        std::unique_ptr<livekit::Room> room_;

        bool sdkInitialized_ = false;
        bool connected_ = false;
    };
}
