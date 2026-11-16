
#include "process_capture.h"

#include <Windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>

#include <wrl.h>
#include <wrl/implements.h>

#include <chrono>
#include <mutex>
#include <utility>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "Mmdevapi.lib")

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::FtmBase;

namespace SashaFM
{
    class ActivationHandler final :
        public RuntimeClass<
            RuntimeClassFlags<ClassicCom>,
            IActivateAudioInterfaceCompletionHandler,
            FtmBase>
    {
    public:
        HRESULT RuntimeClassInitialize(HANDLE eventHandle)
        {
            event = eventHandle;
            return S_OK;
        }

        STDMETHODIMP ActivateCompleted(
            IActivateAudioInterfaceAsyncOperation* operation
        ) override
        {
            HRESULT activationResult = E_UNEXPECTED;
            ComPtr<IUnknown> activatedInterface;

            HRESULT hr = operation->GetActivateResult(
                &activationResult,
                &activatedInterface
            );

            if (SUCCEEDED(hr))
                hr = activationResult;

            {
                std::lock_guard<std::mutex> lock(mutex);

                result = hr;
                audioInterface = activatedInterface;
            }

            SetEvent(event);

            return S_OK;
        }

        HRESULT getResult() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return result;
        }

        ComPtr<IUnknown> getAudioInterface() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return audioInterface;
        }

    private:
        HANDLE event = nullptr;
        mutable std::mutex mutex;
        HRESULT result = E_PENDING;
        ComPtr<IUnknown> audioInterface;
    };

    ProcessCapture::~ProcessCapture()
    {
        stop();
    }

    bool ProcessCapture::start(
        DWORD processId,
        AudioCallback callback
    )
    {
        if (running || captureThread.joinable())
            return false;

        if (processId == 0)
            return false;

        lastError = S_OK;
        running = true;

        std::promise<HRESULT> startupPromise;
        auto startupFuture = startupPromise.get_future();

        captureThread = std::thread(
            &ProcessCapture::captureLoop,
            this,
            processId,
            std::move(callback),
            std::move(startupPromise)
        );

        if (startupFuture.wait_for(std::chrono::seconds(10))
            != std::future_status::ready)
        {
            running = false;

            if (captureThread.joinable())
                captureThread.join();

            lastError = HRESULT_FROM_WIN32(WAIT_TIMEOUT);
            return false;
        }

        HRESULT result = startupFuture.get();

        if (FAILED(result))
        {
            lastError = result;

            if (captureThread.joinable())
                captureThread.join();

            return false;
        }

        return true;
    }

    void ProcessCapture::stop()
    {
        running = false;

        if (captureThread.joinable())
            captureThread.join();
    }

    bool ProcessCapture::isRunning() const
    {
        return running.load();
    }

    HRESULT ProcessCapture::getLastError() const
    {
        return lastError.load();
    }

    void ProcessCapture::captureLoop(
        DWORD processId,
        AudioCallback callback,
        std::promise<HRESULT> startupPromise
    )
    { 
        // All resources are declared before any possible goto.
        bool startupReported = false;
        bool comInitialized = false;
        bool audioStarted = false;

        HRESULT hr = S_OK;

        HANDLE activationEvent = nullptr;
        HANDLE sampleEvent = nullptr;

        WAVEFORMATEX captureFormat = {};
        AUDIOCLIENT_ACTIVATION_PARAMS activationParamsData = {};
        PROPVARIANT activationParams = {};

        ComPtr<ActivationHandler> activationHandler;
        ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
        ComPtr<IUnknown> activatedInterface;
        ComPtr<IAudioClient> audioClient;
        ComPtr<IAudioCaptureClient> captureClient;

        auto reportStartup = [&](HRESULT result)
        {
            if (!startupReported)
            {
                startupPromise.set_value(result);
                startupReported = true;
            }
        };

        // Initialize COM on the capture thread.
        hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        if (FAILED(hr))
        {
            lastError = hr;
            running = false;
            reportStartup(hr);
            return;
        }

        comInitialized = true;

        // Create activation completion event.
        activationEvent = CreateEventW(
            nullptr,
            TRUE,
            FALSE,
            nullptr
        );

        if (!activationEvent)
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            goto cleanup;
        }

        activationHandler = Make<ActivationHandler>();

        if (!activationHandler)
        {
            hr = E_OUTOFMEMORY;
            goto cleanup;
        }

        hr = activationHandler->RuntimeClassInitialize(
            activationEvent
        );

        if (FAILED(hr))
            goto cleanup;

        // Configure process-specific loopback capture.
        activationParamsData.ActivationType =
            AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;

        activationParamsData.ProcessLoopbackParams.TargetProcessId =
            processId;

        activationParamsData.ProcessLoopbackParams.ProcessLoopbackMode =
            PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

        activationParams.vt = VT_BLOB;

        activationParams.blob.cbSize =
            sizeof(activationParamsData);

        activationParams.blob.pBlobData =
            reinterpret_cast<BYTE*>(&activationParamsData);

        // Request the process audio interface.
        hr = ActivateAudioInterfaceAsync(
            VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
            __uuidof(IAudioClient),
            &activationParams,
            activationHandler.Get(),
            &operation
        );

        if (FAILED(hr))
            goto cleanup;

        // Wait for activation to complete.
        {
            DWORD waitResult = WaitForSingleObject(
                activationEvent,
                10000
            );

            if (waitResult != WAIT_OBJECT_0)
            {
                hr = HRESULT_FROM_WIN32(WAIT_TIMEOUT);
                goto cleanup;
            }
        }

        hr = activationHandler->getResult();

        if (FAILED(hr))
            goto cleanup;

        activatedInterface =
            activationHandler->getAudioInterface();

        if (!activatedInterface)
        {
            hr = E_NOINTERFACE;
            goto cleanup;
        }

        hr = activatedInterface.As(&audioClient);

        if (FAILED(hr))
            goto cleanup;

       

        // Create audio packet event.
        sampleEvent = CreateEventW(
            nullptr,
            FALSE,
            FALSE,
            nullptr
        );

        if (!sampleEvent)
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            goto cleanup;
        }

        // Initialize process loopback capture using an explicit audio format.


captureFormat.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
captureFormat.nChannels = 2;
captureFormat.nSamplesPerSec = 44100;
captureFormat.wBitsPerSample = 32;
captureFormat.nBlockAlign = 8;
captureFormat.nAvgBytesPerSec = 44100 * 8;
captureFormat.cbSize = 0;

hr = audioClient->Initialize(
    AUDCLNT_SHAREMODE_SHARED,
    AUDCLNT_STREAMFLAGS_LOOPBACK |
    AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
    AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
    0,
    0,
    &captureFormat,
    nullptr
);

if (FAILED(hr))
    goto cleanup;
        hr = audioClient->SetEventHandle(sampleEvent);

        if (FAILED(hr))
            goto cleanup;

        hr = audioClient->GetService(
            IID_PPV_ARGS(&captureClient)
        );

        if (FAILED(hr))
            goto cleanup;

        hr = audioClient->Start();

        if (FAILED(hr))
            goto cleanup;

        audioStarted = true;

        // Capture initialized successfully.
        reportStartup(S_OK);

        // Continuously receive audio packets.
        while (running)
        {
            DWORD waitResult = WaitForSingleObject(
                sampleEvent,
                100
            );

            if (!running)
                break;

            if (waitResult == WAIT_TIMEOUT)
                continue;

            if (waitResult != WAIT_OBJECT_0)
            {
                hr = HRESULT_FROM_WIN32(GetLastError());
                break;
            }

            UINT32 packetFrames = 0;

            hr = captureClient->GetNextPacketSize(
                &packetFrames
            );

            if (FAILED(hr))
                break;

            while (packetFrames > 0 && running)
            {
                BYTE* audioData = nullptr;
                UINT32 framesAvailable = 0;
                DWORD flags = 0;

                hr = captureClient->GetBuffer(
                    &audioData,
                    &framesAvailable,
                    &flags,
                    nullptr,
                    nullptr
                );

                if (FAILED(hr))
                    break;

                const BYTE* data =
                    (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                    ? nullptr
                    : audioData;

                // The data pointer is valid until ReleaseBuffer.
                if (callback)
                {
                    callback(
                        data,
                        framesAvailable,
                        captureFormat
                    );
                }

                hr = captureClient->ReleaseBuffer(
                    framesAvailable
                );

                if (FAILED(hr))
                    break;

                hr = captureClient->GetNextPacketSize(
                    &packetFrames
                );

                if (FAILED(hr))
                    break;
            }

            if (FAILED(hr))
                break;
        }

    cleanup:

        if (audioStarted && audioClient)
            audioClient->Stop();

        if (FAILED(hr))
            lastError = hr;

        running = false;

        if (!startupReported)
            reportStartup(hr);

      

        if (sampleEvent)
            CloseHandle(sampleEvent);

        if (activationEvent)
            CloseHandle(activationEvent);

        if (comInitialized)
            CoUninitialize();
    }
}
