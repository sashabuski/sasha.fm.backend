
#define UNICODE
#define _UNICODE

#include <Windows.h>
#include <string>
#include <vector>
#include <sstream>
#include <atomic>
#include <cstdint>
#include <cwchar>
#include <cmath>

#include "sources/source_manager.h"
#include "audio/process_capture.h"

namespace
{
    constexpr int ID_SOURCE_COMBO = 1001;
    constexpr int ID_REFRESH = 1002;
    constexpr int ID_CONNECT = 1003;

    constexpr UINT TIMER_REFRESH = 1;

    const COLORREF BG_COLOR = RGB(22, 22, 27);
    const COLORREF TEXT_COLOR = RGB(235, 230, 220);
    const COLORREF MUTED_COLOR = RGB(145, 145, 155);
    const COLORREF LIVE_COLOR = RGB(235, 65, 75);

    struct SourceChoice
    {
        std::wstring label;
        std::wstring key;
    };

    struct AppState
    {
        SashaFM::SourceManager sourceManager;
        SashaFM::ProcessCapture capture;

        HWND window = nullptr;
        HWND titleLabel = nullptr;
        HWND subtitleLabel = nullptr;
        HWND sourceLabel = nullptr;
        HWND sourceCombo = nullptr;
        HWND refreshButton = nullptr;
        HWND connectButton = nullptr;
        HWND statusLabel = nullptr;
        HWND statusDot = nullptr;

        HFONT titleFont = nullptr;
        HFONT normalFont = nullptr;
        HFONT smallFont = nullptr;

        HBRUSH backgroundBrush = nullptr;

        std::vector<SourceChoice> choices;

        std::atomic<uint64_t> capturedFrames{ 0 };
        std::atomic<float> audioRms{ 0.0f };

        bool connected = false;
        bool monitoringStarted = false;
    };

    AppState app;

    void SetControlFont(HWND control, HFONT font)
    {
        SendMessageW(
            control,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(font),
            TRUE
        );
    }

    void CreateControls(HWND window)
    {
        app.titleFont = CreateFontW(
            25, 0, 0, 0, FW_BOLD,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Segoe UI"
        );

        app.normalFont = CreateFontW(
            15, 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Segoe UI"
        );

        app.smallFont = CreateFontW(
            12, 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Segoe UI"
        );

        app.titleLabel = CreateWindowW(
            L"STATIC",
            L"SASHA.FM",
            WS_CHILD | WS_VISIBLE,
            28, 25, 300, 35,
            window, nullptr, nullptr, nullptr
        );

        app.subtitleLabel = CreateWindowW(
            L"STATIC",
            L"BROADCAST CONTROL",
            WS_CHILD | WS_VISIBLE,
            30, 63, 300, 20,
            window, nullptr, nullptr, nullptr
        );

        app.sourceLabel = CreateWindowW(
            L"STATIC",
            L"AUDIO SOURCE",
            WS_CHILD | WS_VISIBLE,
            30, 115, 300, 20,
            window, nullptr, nullptr, nullptr
        );

        app.sourceCombo = CreateWindowExW(
            0,
            L"COMBOBOX",
            L"",
            WS_CHILD | WS_VISIBLE |
            CBS_DROPDOWNLIST | WS_VSCROLL,
            28, 143, 364, 180,
            window,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(ID_SOURCE_COMBO)
            ),
            nullptr,
            nullptr
        );

        app.refreshButton = CreateWindowW(
            L"BUTTON",
            L"Refresh",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            28, 190, 105, 34,
            window,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(ID_REFRESH)
            ),
            nullptr,
            nullptr
        );

        app.connectButton = CreateWindowW(
            L"BUTTON",
            L"CONNECT",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            28, 255, 364, 48,
            window,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(ID_CONNECT)
            ),
            nullptr,
            nullptr
        );

        app.statusDot = CreateWindowW(
            L"STATIC",
            L"",
            WS_CHILD | WS_VISIBLE,
            30, 335, 15, 15,
            window, nullptr, nullptr, nullptr
        );

        app.statusLabel = CreateWindowW(
            L"STATIC",
            L"NOT BROADCASTING",
            WS_CHILD | WS_VISIBLE,
            53, 332, 300, 22,
            window, nullptr, nullptr, nullptr
        );

        SetControlFont(app.titleLabel, app.titleFont);
        SetControlFont(app.subtitleLabel, app.smallFont);
        SetControlFont(app.sourceLabel, app.smallFont);
        SetControlFont(app.sourceCombo, app.normalFont);
        SetControlFont(app.refreshButton, app.normalFont);
        SetControlFont(app.connectButton, app.normalFont);
        SetControlFont(app.statusLabel, app.normalFont);
    }

    void RefreshSourceList()
    {
        if (!app.sourceCombo)
            return;

        std::wstring previousSelection;

        int selected = static_cast<int>(
            SendMessageW(app.sourceCombo, CB_GETCURSEL, 0, 0)
        );

        if (selected != CB_ERR &&
            selected < static_cast<int>(app.choices.size()))
        {
            previousSelection = app.choices[selected].key;
        }

        SendMessageW(app.sourceCombo, CB_RESETCONTENT, 0, 0);
        app.choices.clear();

        const auto sources = app.sourceManager.getSources();
        const auto devices = app.sourceManager.getOutputDevices();

        for (const auto& source : sources)
        {
            if (!source.isActive)
                continue;

            std::wstringstream label;
            label << L"Application: " << source.processName;

            std::wstringstream key;
            key << L"app:"
                << source.processId
                << L":"
                << source.deviceId;

            app.choices.push_back({
                label.str(),
                key.str()
            });
        }

        for (const auto& device : devices)
        {
            app.choices.push_back({
                L"Output device: " + device.name,
                L"device:" + device.id
            });
        }

        int restoredSelection = CB_ERR;

        for (int i = 0; i < static_cast<int>(app.choices.size()); ++i)
        {
            const int index = static_cast<int>(
                SendMessageW(
                    app.sourceCombo,
                    CB_ADDSTRING,
                    0,
                    reinterpret_cast<LPARAM>(
                        app.choices[i].label.c_str()
                    )
                )
            );

            SendMessageW(
                app.sourceCombo,
                CB_SETITEMDATA,
                index,
                static_cast<LPARAM>(i)
            );

            if (app.choices[i].key == previousSelection)
                restoredSelection = index;
        }

        if (restoredSelection != CB_ERR)
        {
            SendMessageW(
                app.sourceCombo,
                CB_SETCURSEL,
                restoredSelection,
                0
            );
        }
        else if (!app.choices.empty())
        {
            SendMessageW(
                app.sourceCombo,
                CB_SETCURSEL,
                0,
                0
            );
        }

        if (app.choices.empty())
        {
            SetWindowTextW(
                app.statusLabel,
                L"NO AUDIO SOURCES FOUND"
            );
        }
        else if (!app.connected)
        {
            SetWindowTextW(
                app.statusLabel,
                L"READY TO CAPTURE"
            );
        }
    }

    void UpdateConnectionUI()
    {
        if (app.connected)
        {
            SetWindowTextW(
                app.connectButton,
                L"DISCONNECT"
            );

            SetWindowTextW(
                app.statusLabel,
                L"CAPTURE STARTED - WAITING FOR AUDIO"
            );
        }
        else
        {
            SetWindowTextW(
                app.connectButton,
                L"CONNECT"
            );

            SetWindowTextW(
                app.statusLabel,
                L"NOT BROADCASTING"
            );
        }

        InvalidateRect(app.window, nullptr, TRUE);
    }

    void HandleConnect()
    {
        if (app.connected)
        {
            app.capture.stop();
            app.connected = false;

            UpdateConnectionUI();
            return;
        }

        const int selected = static_cast<int>(
            SendMessageW(app.sourceCombo, CB_GETCURSEL, 0, 0)
        );

        if (selected == CB_ERR ||
            selected >= static_cast<int>(app.choices.size()))
        {
            MessageBoxW(
                app.window,
                L"Please select an audio source first.",
                L"SASHA.FM",
                MB_OK | MB_ICONINFORMATION
            );

            return;
        }

        const std::wstring key = app.choices[selected].key;

        // Process loopback currently supports application sources only.
        if (key.rfind(L"app:", 0) != 0)
        {
            MessageBoxW(
                app.window,
                L"Output device capture has not been implemented yet.\n\nPlease select an application such as Spotify.",
                L"SASHA.FM",
                MB_OK | MB_ICONINFORMATION
            );

            return;
        }

        const size_t separator = key.find(L':', 4);

        if (separator == std::wstring::npos)
            return;

        DWORD processId = 0;

        try
        {
            processId = static_cast<DWORD>(
                std::stoul(key.substr(4, separator - 4))
            );
        }
        catch (...)
        {
            MessageBoxW(
                app.window,
                L"Could not read the selected application's process ID.",
                L"SASHA.FM",
                MB_OK | MB_ICONERROR
            );

            return;
        }

        app.capturedFrames = 0;
        app.audioRms = 0.0f;

        const bool started = app.capture.start(
            processId,
            [](const BYTE* data,
               UINT32 frameCount,
               const WAVEFORMATEX& format)
            {
                if (frameCount == 0)
                    return;

                app.capturedFrames.fetch_add(
                    frameCount,
                    std::memory_order_relaxed
                );

                if (!data ||
                    format.wFormatTag != WAVE_FORMAT_IEEE_FLOAT ||
                    format.wBitsPerSample != 32 ||
                    format.nChannels == 0)
                {
                    app.audioRms.store(0.0f, std::memory_order_relaxed);
                    return;
                }

                const float* samples =
                    reinterpret_cast<const float*>(data);

                const size_t sampleCount =
                    static_cast<size_t>(frameCount) * format.nChannels;

                double sumSquares = 0.0;

                for (size_t i = 0; i < sampleCount; ++i)
                {
                    const double sample = samples[i];
                    sumSquares += sample * sample;
                }

                const float rms = static_cast<float>(
                    std::sqrt(sumSquares / sampleCount)
                );

                app.audioRms.store(rms, std::memory_order_relaxed);
            }
        );

        if (!started)
        {
            const HRESULT error = app.capture.getLastError();

            wchar_t message[256];

            swprintf_s(
                message,
                L"Could not start audio capture.\n\nError code: 0x%08X",
                static_cast<unsigned int>(error)
            );

            MessageBoxW(
                app.window,
                message,
                L"SASHA.FM",
                MB_OK | MB_ICONERROR
            );

            return;
        }

        app.connected = true;
        UpdateConnectionUI();
    }

    void UpdateCaptureStatus()
    {
        if (!app.connected)
            return;

        if (!app.capture.isRunning())
        {
            app.capture.stop();
            app.connected = false;

            SetWindowTextW(
                app.statusLabel,
                L"CAPTURE STOPPED"
            );

            SetWindowTextW(
                app.connectButton,
                L"CONNECT"
            );

            InvalidateRect(app.window, nullptr, TRUE);
            return;
        }

        if (app.capturedFrames.load(std::memory_order_relaxed) > 0)
        {
            const float rms =
                app.audioRms.load(std::memory_order_relaxed);

            wchar_t status[128];

            if (rms > 0.00001f)
            {
                const double db = 20.0 * std::log10(rms);

                swprintf_s(
                    status,
                    _countof(status),
                    L"AUDIO LEVEL: %.1f dBFS",
                    db
                );
            }
            else
            {
                wcscpy_s(
                    status,
                    _countof(status),
                    L"RECEIVING AUDIO: SILENT"
                );
            }

            SetWindowTextW(app.statusLabel, status);
            InvalidateRect(app.window, nullptr, TRUE);
        }
    }

    LRESULT CALLBACK WindowProcedure(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam
    )
    {
        switch (message)
        {
        case WM_CREATE:
            app.window = window;
            CreateControls(window);
            RefreshSourceList();

            SetTimer(window, TIMER_REFRESH, 1500, nullptr);
            return 0;

        case WM_TIMER:
            if (wParam == TIMER_REFRESH)
            {
                if (app.connected)
                    UpdateCaptureStatus();
                else
                    RefreshSourceList();
            }
            return 0;

        case WM_COMMAND:
        {
            const int controlId = LOWORD(wParam);

            if (controlId == ID_REFRESH)
            {
                RefreshSourceList();
                return 0;
            }

            if (controlId == ID_CONNECT)
            {
                HandleConnect();
                return 0;
            }

            return 0;
        }

        case WM_CTLCOLORSTATIC:
        {
            HDC dc = reinterpret_cast<HDC>(wParam);
            HWND control = reinterpret_cast<HWND>(lParam);

            SetBkColor(dc, BG_COLOR);

            if (control == app.titleLabel)
            {
                SetTextColor(dc, TEXT_COLOR);
            }
            else if (control == app.statusLabel && app.connected)
            {
                SetTextColor(dc, LIVE_COLOR);
            }
            else if (control == app.subtitleLabel ||
                     control == app.sourceLabel)
            {
                SetTextColor(dc, MUTED_COLOR);
            }
            else
            {
                SetTextColor(dc, TEXT_COLOR);
            }

            return reinterpret_cast<LRESULT>(app.backgroundBrush);
        }

        case WM_ERASEBKGND:
        {
            RECT rect;
            GetClientRect(window, &rect);

            FillRect(
                reinterpret_cast<HDC>(wParam),
                &rect,
                app.backgroundBrush
            );

            return 1;
        }

        case WM_DESTROY:
            KillTimer(window, TIMER_REFRESH);

            app.capture.stop();

            if (app.monitoringStarted)
                app.sourceManager.stopMonitoring();

            if (app.titleFont) DeleteObject(app.titleFont);
            if (app.normalFont) DeleteObject(app.normalFont);
            if (app.smallFont) DeleteObject(app.smallFont);
            if (app.backgroundBrush) DeleteObject(app.backgroundBrush);

            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProcW(window, message, wParam, lParam);
    }
}

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int showCommand
)
{
    app.backgroundBrush = CreateSolidBrush(BG_COLOR);

    if (!app.sourceManager.startMonitoring())
    {
        MessageBoxW(
            nullptr,
            L"Could not start audio source monitoring.",
            L"SASHA.FM",
            MB_OK | MB_ICONERROR
        );

        return 1;
    }

    app.monitoringStarted = true;

    const wchar_t CLASS_NAME[] = L"SashaFMWindow";

    WNDCLASSW windowClass = {};

    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = CLASS_NAME;
    windowClass.hbrBackground = app.backgroundBrush;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassW(&windowClass);

    HWND window = CreateWindowExW(
        0,
        CLASS_NAME,
        L"SASHA.FM Broadcaster",
        WS_OVERLAPPED | WS_CAPTION |
        WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        440,
        430,
        nullptr,
        nullptr,
        instance,
        nullptr
    );

    if (!window)
    {
        app.sourceManager.stopMonitoring();
        return 1;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message = {};

    while (GetMessageW(&message, nullptr, 0, 0))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}
