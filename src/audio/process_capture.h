
#pragma once

#include <Windows.h>
#include <audioclient.h>

#include <atomic>
#include <functional>
#include <thread>
#include <future>

namespace SashaFM
{
    class ProcessCapture
    {
    public:
        using AudioCallback = std::function<void(
            const BYTE* data,
            UINT32 frameCount,
            const WAVEFORMATEX& format
        )>;

        ProcessCapture() = default;
        ~ProcessCapture();

        ProcessCapture(const ProcessCapture&) = delete;
        ProcessCapture& operator=(const ProcessCapture&) = delete;

        bool start(DWORD processId, AudioCallback callback);
        void stop();

        bool isRunning() const;
        HRESULT getLastError() const;

    private:
        void captureLoop(
            DWORD processId,
            AudioCallback callback,
            std::promise<HRESULT> startupPromise
        );

        std::atomic<bool> running{ false };
        std::atomic<HRESULT> lastError{ S_OK };

        std::thread captureThread;
    };
}
