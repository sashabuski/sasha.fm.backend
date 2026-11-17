
#include "broadcaster.h"

#include <Windows.h>
#include <winhttp.h>

#include <livekit/livekit.h>
#include <livekit/track.h>
#include <livekit/audio_frame.h>

#include <nlohmann/json.hpp>

#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace SashaFM
{
    namespace
    {
        struct WinHttpHandle
        {
            HINTERNET handle = nullptr;

            explicit WinHttpHandle(HINTERNET value = nullptr)
                : handle(value)
            {
            }

            ~WinHttpHandle()
            {
                if (handle)
                    WinHttpCloseHandle(handle);
            }

            WinHttpHandle(const WinHttpHandle&) = delete;
            WinHttpHandle& operator=(const WinHttpHandle&) = delete;
        };
    }

    Broadcaster::~Broadcaster()
    {
        disconnect();

        if (sdkInitialized_)
        {
            livekit::shutdown();
            sdkInitialized_ = false;
        }
    }

    bool Broadcaster::requestToken(
        std::string& serverUrl,
        std::string& token
    )
    {
        WinHttpHandle session(
            WinHttpOpen(
                L"SASHA.FM Broadcaster",
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                0
            )
        );

        if (!session.handle)
        {
            std::cerr << "Could not initialise WinHTTP.\n";
            return false;
        }

        WinHttpHandle connection(
            WinHttpConnect(
                session.handle,
                L"127.0.0.1",
                3000,
                0
            )
        );

        if (!connection.handle)
        {
            std::cerr << "Could not connect to token server.\n";
            return false;
        }

        WinHttpHandle request(
            WinHttpOpenRequest(
                connection.handle,
                L"POST",
                L"/token",
                nullptr,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                0
            )
        );

        if (!request.handle)
        {
            std::cerr << "Could not create token request.\n";
            return false;
        }

        const std::string body = R"({"role":"broadcaster"})";
        const wchar_t* headers = L"Content-Type: application/json\r\n";

        if (!WinHttpSendRequest(
            request.handle,
            headers,
            static_cast<DWORD>(-1L),
            const_cast<char*>(body.data()),
            static_cast<DWORD>(body.size()),
            static_cast<DWORD>(body.size()),
            0
        ))
        {
            std::cerr << "Could not send token request.\n";
            return false;
        }

        if (!WinHttpReceiveResponse(request.handle, nullptr))
        {
            std::cerr << "Could not receive token response.\n";
            return false;
        }

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);

        if (!WinHttpQueryHeaders(
            request.handle,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode,
            &statusSize,
            WINHTTP_NO_HEADER_INDEX
        ) || statusCode != 200)
        {
            std::cerr << "Token server returned HTTP "
                      << statusCode << ".\n";
            return false;
        }

        std::string response;
        DWORD available = 0;

        do
        {
            available = 0;

            if (!WinHttpQueryDataAvailable(request.handle, &available))
            {
                std::cerr << "Could not read token response.\n";
                return false;
            }

            if (available == 0)
                break;

            std::vector<char> buffer(available);
            DWORD bytesRead = 0;

            if (!WinHttpReadData(
                request.handle,
                buffer.data(),
                available,
                &bytesRead
            ))
            {
                std::cerr << "Could not read token data.\n";
                return false;
            }

            response.append(buffer.data(), bytesRead);
        }
        while (available > 0);

        try
        {
            const auto json = nlohmann::json::parse(response);

            serverUrl = json.at("serverUrl").get<std::string>();
            token = json.at("token").get<std::string>();

            if (serverUrl.empty() || token.empty())
            {
                std::cerr << "Token server returned empty credentials.\n";
                return false;
            }

            return true;
        }
        catch (const std::exception& error)
        {
            std::cerr << "Could not parse token response: "
                      << error.what() << '\n';
            return false;
        }
    }

    bool Broadcaster::connectToTokenServer()
    {
        std::string serverUrl;
        std::string token;

        std::cout << "Requesting LiveKit token...\n";

        if (!requestToken(serverUrl, token))
        {
            std::cerr << "Token request failed.\n";
            return false;
        }

        std::cout << "Token received. Connecting to LiveKit...\n";
        return connect(serverUrl, token);
    }

    bool Broadcaster::connect(
        const std::string& serverUrl,
        const std::string& token
    )
    {
        if (connected_)
            return true;

        if (!sdkInitialized_)
        {
            if (!livekit::initialize())
            {
                std::cerr << "Could not initialise LiveKit.\n";
                return false;
            }

            sdkInitialized_ = true;
        }

        try
        {
            room_ = std::make_unique<livekit::Room>();

            livekit::RoomOptions options;

            std::cout << "LiveKit SDK initialised. Connecting to room...\n";

            const bool success = room_->connect(
                serverUrl,
                token,
                options
            );

            if (!success)
            {
                std::cerr << "Could not connect to LiveKit room.\n";
                room_.reset();
                connected_ = false;
                return false;
            }

            connected_ = true;
            std::cout << "Connected to LiveKit!\n";
            return true;
        }
        catch (const std::exception& error)
        {
            std::cerr << "LiveKit connection error: "
                      << error.what() << '\n';

            room_.reset();
            connected_ = false;
            return false;
        }
    }

    bool Broadcaster::startAudioPublishing()
    {
        if (!connected_ || !room_)
        {
            std::cerr << "Cannot publish audio: not connected to LiveKit.\n";
            return false;
        }

        if (audioTrack_)
            return true;

        try
        {
            auto participant = room_->localParticipant().lock();

            if (!participant)
            {
                std::cerr << "Could not access the local participant.\n";
                return false;
            }

            audioSource_ = std::make_shared<livekit::AudioSource>(
                48000,
                2,
                0
            );

            audioTrack_ = participant->publishAudioTrack(
                "sasha-fm-audio",
                audioSource_,
                livekit::TrackSource::SOURCE_UNKNOWN
            );

            if (!audioTrack_)
            {
                std::cerr << "LiveKit did not create the audio track.\n";
                audioSource_.reset();
                return false;
            }

            std::cout << "LiveKit audio track published.\n";
            return true;
        }
        catch (const std::exception& error)
        {
            std::cerr << "Audio publishing error: "
                      << error.what() << '\n';

            audioTrack_.reset();
            audioSource_.reset();
            return false;
        }
    }

    bool Broadcaster::publishAudioFrame(
        std::vector<std::int16_t> samples
    )
    {
        if (!audioSource_ || !audioTrack_)
            return false;

        // A 10 ms frame at 48 kHz stereo contains
        // 480 samples per channel, or 960 interleaved samples.
        if (samples.size() != 960)
            return false;

        try
        {
            livekit::AudioFrame frame(
                std::move(samples),
                48000,
                2,
                480
            );

            audioSource_->captureFrame(frame, 20);
            return true;
        }
        catch (const std::exception& error)
        {
            std::cerr << "Could not publish audio frame: "
                      << error.what() << '\n';
            return false;
        }
    }

    void Broadcaster::stopAudioPublishing()
    {
        audioTrack_.reset();
        audioSource_.reset();

        std::cout << "Audio publishing stopped.\n";
    }

    void Broadcaster::disconnect()
    {
        stopAudioPublishing();

        if (room_)
        {
            if (connected_)
                room_->disconnect();

            room_.reset();
        }

        connected_ = false;
        std::cout << "Disconnected from LiveKit.\n";
    }

    bool Broadcaster::isConnected() const noexcept
    {
        return connected_;
    }

    bool Broadcaster::isAudioPublished() const noexcept
    {
        return audioTrack_ != nullptr;
    }
}
