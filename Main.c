#pragma warning(push, 0)
#include <stdio.h>
#include <windows.h>
#include <psapi.h>
#include <emmintrin.h>
#pragma warning(pop)

#include <stdint.h>
#include "Main.h"

#pragma comment(lib, "Winmm.lib")

HWND gGameWindow;
BOOL gGameIsRunning;
GAMEBITMAP gBackBuffer;
GAMEPERFORMANCEDATA gPerformanceData;
PLAYER gPlayer;
BOOL gWindowHasFocus;

int __stdcall WinMain(HINSTANCE instance, HINSTANCE previousInstance, LPSTR commandLine, int showCommand) {
	UNREFERENCED_PARAMETER(previousInstance);
	UNREFERENCED_PARAMETER(commandLine);
	UNREFERENCED_PARAMETER(showCommand);

    MSG message = { 0 };
    int64_t frameStart = 0;
    int64_t frameEnd = 0;
    int64_t elapsedMicroseconds = 0;
    int64_t elapsedMicrosecondsAccumulatorRaw = 0;
    int64_t elapsedMicrosecondsAccumulatorCooked = 0;

    HMODULE NtDllModuleHandle = NULL;

    FILETIME processCreationTime = { 0 };
    FILETIME processExitTime = { 0 };

    int64_t currentUserCPUTTime = 0;
    int64_t currentKernelCPUTime = 0;
    int64_t previousUserCPUTime = 0;
    int64_t previousKernelCPUTTime = 0;

    HANDLE processHandle = GetCurrentProcess();

    if ((NtDllModuleHandle = GetModuleHandleA("ntdll.dll")) == NULL) {
        MessageBoxA(NULL, "Couldn't find ntdll.dll!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        goto Exit;
    }

    if ((NtQueryTimerResolution = (_NtQueryTimerResolution)GetProcAddress(NtDllModuleHandle, "NtQueryTimerResolution")) == NULL) {
        MessageBoxA(NULL, "Couldn't find NtQueryTimerResolution function in ntdll.dll!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        goto Exit;
    }

    NtQueryTimerResolution(&gPerformanceData.minimumTimerResolution, &gPerformanceData.maximumTimerResolution, &gPerformanceData.currentTimerResolution);

    GetSystemInfo(&gPerformanceData.systemInfo);
    GetSystemTimeAsFileTime((FILETIME*)&gPerformanceData.previousSystemTime);

    if (GameIsAlreadyRunning() == TRUE) {
        MessageBoxA(NULL, "Another instance of this program is running!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        goto Exit;
    }

    if (timeBeginPeriod(1) == TIMERR_NOCANDO) {
        MessageBoxA(NULL, "Failed to set global timer resolution!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        goto Exit;
    }

    if (SetPriorityClass(processHandle, HIGH_PRIORITY_CLASS) == 0) {
        MessageBoxA(NULL, "Failed to set process priority!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        goto Exit;
    }

    if (SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST) == 0) {
        MessageBoxA(NULL, "Failed to set thread priority!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        goto Exit;
    }

    if (CreateMainGameWindow() != ERROR_SUCCESS) {
        goto Exit;
    }

    QueryPerformanceFrequency((LARGE_INTEGER*) &gPerformanceData.perfFrequency);

    gBackBuffer.bitmapinfo.bmiHeader.biSize = sizeof(gBackBuffer.bitmapinfo.bmiHeader);
    gBackBuffer.bitmapinfo.bmiHeader.biWidth = GAME_RES_WIDTH;
    gBackBuffer.bitmapinfo.bmiHeader.biHeight = GAME_RES_HEIGHT;
    gBackBuffer.bitmapinfo.bmiHeader.biBitCount = GAME_BPP;
    gBackBuffer.bitmapinfo.bmiHeader.biCompression = BI_RGB;
    gBackBuffer.bitmapinfo.bmiHeader.biPlanes = 1;
    gBackBuffer.memory = VirtualAlloc(NULL, GAME_DRAWING_AREA_MEMORY_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    if (gBackBuffer.memory == NULL) {
        MessageBoxA(NULL, "Failed to allocate memory for drawing surface!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        goto Exit;
    }

    memset(gBackBuffer.memory, 0x7f, GAME_DRAWING_AREA_MEMORY_SIZE);

    gPlayer.screenPosX = 30;
    gPlayer.screenPosY = 30;

    gGameIsRunning = TRUE;

    while (gGameIsRunning) {
        QueryPerformanceCounter((LARGE_INTEGER*) &frameStart);

        while (PeekMessageA(&message, gGameWindow, 0, 0, PM_REMOVE)) {
            DispatchMessageA(&message);
        }

        ProcessPlayerInput();
        RenderFrameGraphics();

        QueryPerformanceCounter((LARGE_INTEGER*)&frameEnd);

        elapsedMicroseconds = frameEnd - frameStart;
        elapsedMicroseconds *= 1000000;
        elapsedMicroseconds /= gPerformanceData.perfFrequency;

        gPerformanceData.totalFramesRendered++;
        elapsedMicrosecondsAccumulatorRaw += elapsedMicroseconds;

        while (elapsedMicroseconds < TARGET_MICROSECONDS_PER_FRAME) {
            elapsedMicroseconds = frameEnd - frameStart;
            elapsedMicroseconds *= 1000000;
            elapsedMicroseconds /= gPerformanceData.perfFrequency;

            QueryPerformanceCounter((LARGE_INTEGER*)&frameEnd);

            if (elapsedMicroseconds < TARGET_MICROSECONDS_PER_FRAME * 0.75f) {
                Sleep(1);
            }
        }

        elapsedMicrosecondsAccumulatorCooked += elapsedMicroseconds;

        if ((gPerformanceData.totalFramesRendered % CALCULATE_AVG_FPS_EVERY_X_FRAMES) == 0) {
            GetSystemTimeAsFileTime(&gPerformanceData.currentSystemTime);
            GetProcessTimes(processHandle,
                            &processCreationTime,
                            &processExitTime,
                            &currentKernelCPUTime,
                            &currentUserCPUTTime);

            gPerformanceData.cpuPercent = (currentKernelCPUTime - previousKernelCPUTTime) +
                    (currentUserCPUTTime - previousUserCPUTime);
            gPerformanceData.cpuPercent /= (gPerformanceData.currentSystemTime - gPerformanceData.previousSystemTime);
            gPerformanceData.cpuPercent /= gPerformanceData.systemInfo.dwNumberOfProcessors;
            gPerformanceData.cpuPercent *= 100;

            GetProcessHandleCount(processHandle, &gPerformanceData.handleCount);
            K32GetProcessMemoryInfo(processHandle, (PROCESS_MEMORY_COUNTERS*)&gPerformanceData.memInfo, sizeof(gPerformanceData.memInfo));

            gPerformanceData.rawFPSAverage = 1.0f / (((float)elapsedMicrosecondsAccumulatorRaw / CALCULATE_AVG_FPS_EVERY_X_FRAMES) * 0.000001f);
            gPerformanceData.cookedFPSAverage = 1.0f / (((float)elapsedMicrosecondsAccumulatorCooked / CALCULATE_AVG_FPS_EVERY_X_FRAMES) * 0.000001f);

            elapsedMicrosecondsAccumulatorRaw = 0;
            elapsedMicrosecondsAccumulatorCooked = 0;

            previousKernelCPUTTime = currentKernelCPUTime;
            previousUserCPUTime = currentUserCPUTTime;
            gPerformanceData.previousSystemTime = gPerformanceData.currentSystemTime;
        }
    }

Exit:
    return 0;
}

LRESULT CALLBACK MainWindowProc(_In_ HWND windowHandle, _In_ UINT message, _In_ WPARAM wParam, _In_ LPARAM lParam) {
    LRESULT result = 0;

    switch (message) {
        case WM_CLOSE:
            gGameIsRunning = FALSE;
            PostQuitMessage(0);
            break;
        case WM_ACTIVATE:
            if (wParam == 0) {
                // Our window has lost focus
                gWindowHasFocus = FALSE;
            }
            else {
                // Our window has focus
                ShowCursor(FALSE);
                gWindowHasFocus = TRUE;
            }
            break;
        default:
            result = DefWindowProcA(windowHandle, message, wParam, lParam);
    }

    return result;
}

DWORD CreateMainGameWindow(void) {
    DWORD result = ERROR_SUCCESS;
    WNDCLASSEXA windowClass = {0};

    windowClass.cbSize = sizeof(WNDCLASSEXA);
    windowClass.style = 0;
    windowClass.lpfnWndProc = (WNDPROC) MainWindowProc;
    windowClass.cbClsExtra = 0;
    windowClass.cbWndExtra = 0;
    windowClass.hInstance = GetModuleHandleA(NULL);
    windowClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    windowClass.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    windowClass.hbrBackground = CreateSolidBrush(RGB(255, 0, 255));
    windowClass.lpszMenuName =  NULL;
    windowClass.lpszClassName = GAME_NAME "_WindowClass";

    if (!RegisterClassExA(&windowClass)) {
        result = GetLastError();

        MessageBox(NULL, "Window Registration Failed!", "Error!",
                   MB_ICONEXCLAMATION | MB_OK);
        goto Exit;
    }

    gGameWindow = CreateWindowExA(0, windowClass.lpszClassName, "Game B", WS_VISIBLE,
                                   CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL, windowClass.hInstance, NULL);

    if (!gGameWindow) {
        result = GetLastError();
        MessageBox(NULL, "Window Creation Failed!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        goto Exit;
    }

    gPerformanceData.monitorInfo.cbSize = sizeof(MONITORINFO);

    if(GetMonitorInfoA(MonitorFromWindow(gGameWindow, MONITOR_DEFAULTTOPRIMARY), &gPerformanceData.monitorInfo) == 0) {
        result = ERROR_INVALID_MONITOR_HANDLE;
        goto Exit;
    }

    gPerformanceData.monitorWidth = gPerformanceData.monitorInfo.rcMonitor.right - gPerformanceData.monitorInfo.rcMonitor.left;
    gPerformanceData.monitorHeight = gPerformanceData.monitorInfo.rcMonitor.bottom - gPerformanceData.monitorInfo.rcMonitor.top;

    if (SetWindowLongPtrA(gGameWindow, GWL_STYLE, WS_VISIBLE) == 0) {
        result = GetLastError();
        goto Exit;
    }

    if (SetWindowPos(gGameWindow, HWND_TOP, gPerformanceData.monitorInfo.rcMonitor.left, gPerformanceData.monitorInfo.rcMonitor.top,
                     gPerformanceData.monitorWidth, gPerformanceData.monitorHeight, SWP_FRAMECHANGED) == 0) {
        result = GetLastError();
        goto Exit;
    }

Exit:
    return result;
}

BOOL GameIsAlreadyRunning(void) {
    CreateMutexA(NULL, FALSE, GAME_NAME "_GameMutex");

    return (GetLastError() == ERROR_ALREADY_EXISTS);
}

void ProcessPlayerInput(void) {

    if (gWindowHasFocus == FALSE) {
        return;
    }

    int16_t escapeKeyIsDown = GetAsyncKeyState(VK_ESCAPE);
    int16_t debugKeyIsDown = GetAsyncKeyState(VK_F1);
    int16_t leftKeyIsDown = GetAsyncKeyState(VK_LEFT) | GetAsyncKeyState('A');
    int16_t rightKeyIsDown = GetAsyncKeyState(VK_RIGHT) | GetAsyncKeyState('D');
    int16_t upKeyIsDown = GetAsyncKeyState(VK_UP) | GetAsyncKeyState('W');
    int16_t downKeyIsDown = GetAsyncKeyState(VK_DOWN) | GetAsyncKeyState('S');

    static int16_t debugKeyWasDown;
    static int16_t leftKeyWasDown;
    static int16_t rightKeyWasDown;
    static int16_t upKeyWasDown;
    static int16_t downKeyWasDown;

    if (escapeKeyIsDown) {
        SendMessageA(gGameWindow, WM_CLOSE, 0, 0);
    }

    if (debugKeyIsDown && !debugKeyWasDown) {
        gPerformanceData.displayDebugInfo = !gPerformanceData.displayDebugInfo;
    }

    if (leftKeyIsDown) {
        if (gPlayer.screenPosX > 0) {
            gPlayer.screenPosX--;
        }
    }

    if (rightKeyIsDown) {
        if (gPlayer.screenPosX < GAME_RES_WIDTH - 16) {
            gPlayer.screenPosX++;
        }
    }

    if (downKeyIsDown) {
        if (gPlayer.screenPosY < GAME_RES_HEIGHT - 16) {
            gPlayer.screenPosY++;
        }
    }

    if (upKeyIsDown) {
        if (gPlayer.screenPosY > 0) {
            gPlayer.screenPosY--;
        }

    }

    debugKeyWasDown = debugKeyIsDown;
    leftKeyWasDown = leftKeyIsDown;
    rightKeyWasDown = rightKeyIsDown;
    upKeyWasDown = upKeyIsDown;
    downKeyWasDown = downKeyIsDown;
}

void RenderFrameGraphics(void) {
#ifdef SIMD
    __m128i quadPixel = { 0x7f, 0x00, 0x00, 0xff, 0x7f, 0x00, 0x00, 0xff, 0x7f, 0x00, 0x00, 0xff, 0x7f, 0x00, 0x00, 0xff };

    ClearScreen(&quadPixel);
#else
    PIXEL32 pixel = { 0x7f, 0x00, 0x00, 0xff };

    ClearScreen(&pixel);
#endif

    int32_t screenX = gPlayer.screenPosX;
    int32_t screenY = gPlayer.screenPosY;
    int32_t startingScreenPixel = ((GAME_RES_WIDTH * GAME_RES_HEIGHT) - GAME_RES_WIDTH) - (GAME_RES_WIDTH * screenY) + screenX;

    for (int32_t y = 0;y < 16; y++) {
        for (int32_t x = 0; x < 16; x++) {
            memset((PIXEL32*)gBackBuffer.memory + (uintptr_t)startingScreenPixel + x - ((uintptr_t)GAME_RES_WIDTH * y), 0xff, sizeof(PIXEL32));
        }
    }

    HDC deviceContext = GetDC(gGameWindow);

    StretchDIBits(deviceContext, 0, 0, gPerformanceData.monitorWidth, gPerformanceData.monitorHeight, 0,0,
                  GAME_RES_WIDTH,GAME_RES_HEIGHT, gBackBuffer.memory, &gBackBuffer.bitmapinfo, DIB_RGB_COLORS, SRCCOPY);

    if (gPerformanceData.displayDebugInfo) {
        SelectObject(deviceContext, (HFONT) GetStockObject(ANSI_FIXED_FONT));
        char DebugTextBuffer[64] = { 0 };
        sprintf_s(DebugTextBuffer, sizeof(DebugTextBuffer), "FPS Raw:        %.01f", gPerformanceData.rawFPSAverage);
        TextOutA(deviceContext,10 , 10, DebugTextBuffer, (int)strlen(DebugTextBuffer));
        sprintf_s(DebugTextBuffer, sizeof(DebugTextBuffer), "FPS Cooked:     %.01f  ", gPerformanceData.cookedFPSAverage);
        TextOutA(deviceContext,10 , 23, DebugTextBuffer, (int)strlen(DebugTextBuffer));

        sprintf_s(DebugTextBuffer, sizeof(DebugTextBuffer), "Min. Timer Res: %.02f ", gPerformanceData.minimumTimerResolution / 10000.0f);
        TextOutA(deviceContext,10 , 36, DebugTextBuffer, (int)strlen(DebugTextBuffer));
        sprintf_s(DebugTextBuffer, sizeof(DebugTextBuffer), "Max. Timer Res: %.02f  ", gPerformanceData.maximumTimerResolution / 10000.0f);
        TextOutA(deviceContext,10 , 49, DebugTextBuffer, (int)strlen(DebugTextBuffer));
        sprintf_s(DebugTextBuffer, sizeof(DebugTextBuffer), "Cur. Timer Res: %.02f  ", gPerformanceData.currentTimerResolution / 10000.0f);
        TextOutA(deviceContext,10 , 62, DebugTextBuffer, (int)strlen(DebugTextBuffer));

        sprintf_s(DebugTextBuffer, sizeof(DebugTextBuffer), "Handles:        %lu   ", gPerformanceData.handleCount);
        TextOutA(deviceContext,10 , 75, DebugTextBuffer, (int)strlen(DebugTextBuffer));
        sprintf_s(DebugTextBuffer, sizeof(DebugTextBuffer), "Memory:       %lu KB ", gPerformanceData.memInfo.PrivateUsage / 1024);
        TextOutA(deviceContext,10 , 88, DebugTextBuffer, (int)strlen(DebugTextBuffer));

        sprintf_s(DebugTextBuffer, sizeof(DebugTextBuffer), "CPU Perc.:    %.02f %% ", gPerformanceData.cpuPercent);
        TextOutA(deviceContext,10 , 101, DebugTextBuffer, (int)strlen(DebugTextBuffer));

    }

    ReleaseDC(gGameWindow, deviceContext);
}

#ifdef SIMD
__forceinline void ClearScreen(_In_ __m128i* color) {
    for (int i = 0; i < GAME_RES_WIDTH * GAME_RES_HEIGHT; i += 4) {
        _mm_store_si128((PIXEL32*) gBackBuffer.memory + i, *color);
    }
}
#else
__forceinline void ClearScreen(_In_ PIXEL32* pixel) {
    for (int i = 0; i < GAME_RES_WIDTH * GAME_RES_HEIGHT; i++) {
        memcpy((PIXEL32*)gBackBuffer.memory + i, pixel, sizeof(PIXEL32));
    }
}
#endif