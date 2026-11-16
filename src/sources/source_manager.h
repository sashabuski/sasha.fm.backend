
#pragma once

#include <string>
#include <vector>
#include <Windows.h>

#include <atomic>
#include <mutex>
#include <thread>

namespace SashaFM
{
    struct AudioSource
    {
        DWORD processId;
        std::wstring processName;
        std::wstring deviceName;
        std::wstring deviceId;
        bool isActive;
    };

    struct AudioOutputDevice
    {
        std::wstring name;
        std::wstring id;
    };

    class SourceManager
    {
    public:
        SourceManager() = default;
        ~SourceManager();

        SourceManager(const SourceManager&) = delete;
        SourceManager& operator=(const SourceManager&) = delete;

        bool startMonitoring();
        void stopMonitoring();

        std::vector<AudioSource> getSources() const;
        std::vector<AudioOutputDevice> getOutputDevices() const;

    private:
        void monitoringLoop();
        void refreshSources();

        std::atomic<bool> monitoring{ false };
        std::thread monitoringThread;

        mutable std::mutex dataMutex;

        std::vector<AudioSource> sources;
        std::vector<AudioOutputDevice> outputDevices;
    };
}
