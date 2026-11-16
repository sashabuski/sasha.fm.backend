
#include "source_manager.h"

#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>
#include <utility>
#include <algorithm>
#include <chrono>
#include <set>
#include <string>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "propsys.lib")

namespace SashaFM
{
    namespace
    {
        std::wstring getProcessName(DWORD processId)
        {
            HANDLE process = OpenProcess(
                PROCESS_QUERY_LIMITED_INFORMATION,
                FALSE,
                processId
            );

            if (!process)
                return L"Unknown application";

            wchar_t path[MAX_PATH];
            DWORD size = MAX_PATH;

            std::wstring name = L"Unknown application";

            if (QueryFullProcessImageNameW(
                process,
                0,
                path,
                &size))
            {
                std::wstring fullPath(path);

                const size_t separator =
                    fullPath.find_last_of(L"\\/");

                name = separator == std::wstring::npos
                    ? fullPath
                    : fullPath.substr(separator + 1);
            }

            CloseHandle(process);

            return name;
        }

        std::wstring getDeviceName(IMMDevice* device)
        {
            IPropertyStore* properties = nullptr;

            if (FAILED(device->OpenPropertyStore(
                STGM_READ,
                &properties)))
            {
                return L"Unknown device";
            }

            PROPVARIANT value;
            PropVariantInit(&value);

            std::wstring name = L"Unknown device";

            if (SUCCEEDED(properties->GetValue(
                PKEY_Device_FriendlyName,
                &value)))
            {
                if (value.vt == VT_LPWSTR && value.pwszVal)
                    name = value.pwszVal;
            }

            PropVariantClear(&value);
            properties->Release();

            return name;
        }

        std::wstring getDeviceId(IMMDevice* device)
        {
            LPWSTR id = nullptr;

            if (FAILED(device->GetId(&id)) || !id)
                return L"";

            std::wstring result(id);
            CoTaskMemFree(id);

            return result;
        }
    }

    SourceManager::~SourceManager()
    {
        stopMonitoring();
    }

    bool SourceManager::startMonitoring()
    {
        bool expected = false;

        if (!monitoring.compare_exchange_strong(
            expected,
            true))
        {
            return false;
        }

        try
        {
            monitoringThread = std::thread(
                &SourceManager::monitoringLoop,
                this
            );
        }
        catch (...)
        {
            monitoring = false;
            return false;
        }

        return true;
    }

    void SourceManager::stopMonitoring()
    {
        if (!monitoring.exchange(false))
            return;

        if (monitoringThread.joinable())
            monitoringThread.join();
    }

    std::vector<AudioSource> SourceManager::getSources() const
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        return sources;
    }

    std::vector<AudioOutputDevice>
        SourceManager::getOutputDevices() const
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        return outputDevices;
    }

    void SourceManager::monitoringLoop()
    {
        const HRESULT comResult = CoInitializeEx(
            nullptr,
            COINIT_MULTITHREADED
        );

        if (FAILED(comResult))
        {
            monitoring = false;
            return;
        }

        while (monitoring)
        {
            refreshSources();

            // Check the stop flag regularly rather than
            // waiting for the entire one-second interval.
            for (int i = 0; i < 10 && monitoring; ++i)
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(100)
                );
            }
        }

        CoUninitialize();
    }

    void SourceManager::refreshSources()
    {
        std::vector<AudioSource> newSources;
        std::vector<AudioOutputDevice> newOutputDevices;

        IMMDeviceEnumerator* enumerator = nullptr;

        HRESULT result = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            reinterpret_cast<void**>(&enumerator)
        );

        if (FAILED(result))
            return;

        IMMDeviceCollection* devices = nullptr;

        result = enumerator->EnumAudioEndpoints(
            eRender,
            DEVICE_STATE_ACTIVE,
            &devices
        );

        if (FAILED(result))
        {
            enumerator->Release();
            return;
        }

        UINT deviceCount = 0;
        devices->GetCount(&deviceCount);

        // Avoid duplicate entries for the same process
        // on the same output device.
        std::set<std::wstring> seenSessions;

        for (UINT i = 0; i < deviceCount; ++i)
        {
            IMMDevice* device = nullptr;

            if (FAILED(devices->Item(i, &device)))
                continue;

            const std::wstring deviceName =
                getDeviceName(device);

            const std::wstring deviceId =
                getDeviceId(device);

            newOutputDevices.push_back({
                deviceName,
                deviceId
            });

            IAudioSessionManager2* sessionManager = nullptr;

            result = device->Activate(
                __uuidof(IAudioSessionManager2),
                CLSCTX_ALL,
                nullptr,
                reinterpret_cast<void**>(&sessionManager)
            );

            if (SUCCEEDED(result))
            {
                IAudioSessionEnumerator* sessions = nullptr;

                result = sessionManager->GetSessionEnumerator(
                    &sessions
                );

                if (SUCCEEDED(result))
                {
                    int sessionCount = 0;
                    sessions->GetCount(&sessionCount);

                    for (int j = 0; j < sessionCount; ++j)
                    {
                        IAudioSessionControl* control = nullptr;

                        if (FAILED(sessions->GetSession(
                            j,
                            &control)))
                        {
                            continue;
                        }

                        AudioSessionState state;

                        if (SUCCEEDED(control->GetState(&state)))
                        {
                            IAudioSessionControl2* control2 = nullptr;

                            if (SUCCEEDED(control->QueryInterface(
                                __uuidof(IAudioSessionControl2),
                                reinterpret_cast<void**>(&control2))))
                            {
                                DWORD processId = 0;

                                if (SUCCEEDED(
                                    control2->GetProcessId(&processId)) &&
                                    processId != 0)
                                {
                                    const std::wstring key =
                                        std::to_wstring(processId) +
                                        L"|" + deviceId;

                                    if (seenSessions.insert(key).second)
                                    {
                                        AudioSource source;

                                        source.processId = processId;
                                        source.processName =
                                            getProcessName(processId);
                                        source.deviceName = deviceName;
                                        source.deviceId = deviceId;
                                        source.isActive =
                                            state == AudioSessionStateActive;

                                        newSources.push_back(
                                            std::move(source)
                                        );
                                    }
                                }

                                control2->Release();
                            }
                        }

                        control->Release();
                    }

                    sessions->Release();
                }

                sessionManager->Release();
            }

            device->Release();
        }

        devices->Release();
        enumerator->Release();

        // Replace the current snapshot safely.
        {
            std::lock_guard<std::mutex> lock(dataMutex);

            sources = std::move(newSources);
            outputDevices = std::move(newOutputDevices);
        }
    }
}
