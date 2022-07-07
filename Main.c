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
GAMEBITMAP g6x7Font;
GAMEPERFORMANCEDATA gPerformanceData;
HERO gPlayer;
BOOL gWindowHasFocus;
REGISTRYPARAMS gRegistryParams;

int __stdcall WinMain(HINSTANCE instance, HINSTANCE previousInstance, LPSTR commandLine, int showCommand) {
    UNREFERENCED_PARAMETER(instance);
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
    
    if (LoadRegistryParameters() != ERROR_SUCCESS) {
        goto Exit;
    }

    if ((NtDllModuleHandle = GetModuleHandleA("ntdll.dll")) == NULL) {
        LogMessageA(LOG_ERROR, "[%s] Couldn't load ntdll.dll! Error 0x%08lx!", __FUNCTION__, GetLastError());
        MessageBoxA(NULL, "Couldn't load ntdll.dll!", "Error!", MB_ICONERROR | MB_OK);
        goto Exit;
    }

    if ((NtQueryTimerResolution = (_NtQueryTimerResolution)GetProcAddress(NtDllModuleHandle, "NtQueryTimerResolution")) == NULL) {
        LogMessageA(LOG_ERROR, "[%s] Couldn't find NtQueryTimerResolution function in ntdll.dll! Error 0x%08lx!", __FUNCTION__, GetLastError());
        MessageBoxA(NULL, "Couldn't find NtQueryTimerResolution function in ntdll.dll!", "Error!", MB_ICONERROR | MB_OK);
        goto Exit;
    }

    NtQueryTimerResolution(&gPerformanceData.minimumTimerResolution, &gPerformanceData.maximumTimerResolution, &gPerformanceData.currentTimerResolution);

    GetSystemInfo(&gPerformanceData.systemInfo);
    GetSystemTimeAsFileTime((FILETIME*)&gPerformanceData.previousSystemTime);

    if (GameIsAlreadyRunning() == TRUE) {
        LogMessageA(LOG_ERROR, "[%s] Another instance of this program is running!", __FUNCTION__);
        MessageBoxA(NULL, "Another instance of this program is running!", "Error!", MB_ICONERROR | MB_OK);
        goto Exit;
    }

    if (timeBeginPeriod(1) == TIMERR_NOCANDO) {
        LogMessageA(LOG_ERROR, "[%s] Failed to set global timer resolution!", __FUNCTION__);
        MessageBoxA(NULL, "Failed to set global timer resolution!", "Error!", MB_ICONERROR | MB_OK);
        goto Exit;
    }

    if (SetPriorityClass(processHandle, HIGH_PRIORITY_CLASS) == 0) {
        LogMessageA(LOG_ERROR, "[%s] Failed to set process priority! Error 0x%08lx!", __FUNCTION__, GetLastError());
        MessageBoxA(NULL, "Failed to set process priority!", "Error!", MB_ICONERROR | MB_OK);
        goto Exit;
    }

    if (SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST) == 0) {
        LogMessageA(LOG_ERROR, "[%s] Failed to set thread priority! Error 0x%08lx!", __FUNCTION__, GetLastError());
        MessageBoxA(NULL, "Failed to set thread priority!", "Error!", MB_ICONERROR | MB_OK);
        goto Exit;
    }

    if (LoadRegistryParameters() != ERROR_SUCCESS) {
        goto Exit;
    }

    if (CreateMainGameWindow() != ERROR_SUCCESS) {
        LogMessageA(LOG_ERROR, "[%s] CreateMainGameWindow failed!", __FUNCTION__);
        goto Exit;
    }

    if ((Load32BppBitmapFromFile("..\\Assets\\6x7Font.bmpx", &g6x7Font)) != ERROR_SUCCESS) {
        LogMessageA(LOG_ERROR, "[%s] Failed to load 6x7Font!", __FUNCTION__);
        MessageBoxA(NULL, "Failed to load 6x7Font!", "Error!", MB_ICONERROR | MB_OK);
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
        LogMessageA(LOG_ERROR, "[%s] Failed to allocate memory for drawing surface!", __FUNCTION__);
        MessageBoxA(NULL, "Failed to allocate memory for drawing surface!", "Error!", MB_ICONERROR | MB_OK);
        goto Exit;
    }

    memset(gBackBuffer.memory, 0x7f, GAME_DRAWING_AREA_MEMORY_SIZE);

    if (InitializeHero() != ERROR_SUCCESS) {
        LogMessageA(LOG_ERROR, "[%s] Failed to initialize hero!", __FUNCTION__);
        MessageBoxA(NULL, "Failed to initialize hero!", "Error!", MB_ICONERROR | MB_OK);
        goto Exit;
    }

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
            GetSystemTimeAsFileTime((FILETIME*) &gPerformanceData.currentSystemTime);
            GetProcessTimes(processHandle,
                            &processCreationTime,
                            &processExitTime,
                            (FILETIME*) &currentKernelCPUTime,
                            (FILETIME*) &currentUserCPUTTime);

            gPerformanceData.cpuPercent = (double)(currentKernelCPUTime - previousKernelCPUTTime) +
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

        LogMessageA(LOG_ERROR, "[%s] Window Registration Failed! Error 0x%08lx!", __FUNCTION__, result);
        MessageBox(NULL, "Window Registration Failed!", "Error!",
                   MB_ICONERROR | MB_OK);
        goto Exit;
    }

    gGameWindow = CreateWindowExA(0, windowClass.lpszClassName, "Game B", WS_VISIBLE,
                                   CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL, windowClass.hInstance, NULL);

    if (!gGameWindow) {
        result = GetLastError();
        LogMessageA(LOG_ERROR, "[%s] Window Creation Failed! Error 0x%08lx!", __FUNCTION__, result);
        MessageBox(NULL, "Window Creation Failed!", "Error!", MB_ICONERROR | MB_OK);
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
        LogMessageA(LOG_ERROR, "[%s] SetWindowLongPtrA Failed! Error 0x%08lx!", __FUNCTION__, result);
        goto Exit;
    }

    if (SetWindowPos(gGameWindow, HWND_TOP, gPerformanceData.monitorInfo.rcMonitor.left, gPerformanceData.monitorInfo.rcMonitor.top,
                     gPerformanceData.monitorWidth, gPerformanceData.monitorHeight, SWP_FRAMECHANGED) == 0) {
        result = GetLastError();
        LogMessageA(LOG_ERROR, "[%s] SetWindowPos Failed! Error 0x%08lx!", __FUNCTION__, result);
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

    if (!gPlayer.movementRemaining) {
        if (downKeyIsDown) {
            if (gPlayer.screenPosY < GAME_RES_HEIGHT - 16) {
                gPlayer.direction = DOWN;
                gPlayer.movementRemaining = 16;
            }
        } 
        else if (leftKeyIsDown) {
            if (gPlayer.screenPosY < GAME_RES_HEIGHT - 16) {
                gPlayer.direction = LEFT;
                gPlayer.movementRemaining = 16;
            }
        } 
        else if (rightKeyIsDown) {
            if (gPlayer.screenPosX < GAME_RES_WIDTH - 16) {
                gPlayer.direction = RIGHT;
                gPlayer.movementRemaining = 16;
            }
        } 
        else if (upKeyIsDown) {
            if (gPlayer.screenPosY > 0) {
                gPlayer.direction = UP;
                gPlayer.movementRemaining = 16;
            }
        }
    }
    else {
        gPlayer.movementRemaining--;

        if (gPlayer.direction == DOWN) {
            gPlayer.screenPosY++;
        }
        else if (gPlayer.direction == LEFT) {
            gPlayer.screenPosX--;
        }
        else if (gPlayer.direction == RIGHT) {
            gPlayer.screenPosX++;
        }
        else if (gPlayer.direction == UP) {
            gPlayer.screenPosY--;
        }

        switch (gPlayer.movementRemaining) {
            case 16:
                gPlayer.spriteIndex = 0;
                break;
            case 12:
                gPlayer.spriteIndex = 1;
                break;
            case 8:
                gPlayer.spriteIndex = 0;
                break;
            case 4:
                gPlayer.spriteIndex = 2;
                break;
            case 0:
                gPlayer.spriteIndex = 0;
                break;
        }
    }

    debugKeyWasDown = debugKeyIsDown;
    leftKeyWasDown = leftKeyIsDown;
    rightKeyWasDown = rightKeyIsDown;
    upKeyWasDown = upKeyIsDown;
    downKeyWasDown = downKeyIsDown;
}

DWORD Load32BppBitmapFromFile(_In_ char* fileName, _Inout_ GAMEBITMAP* gameBitmap) {
    DWORD error = ERROR_SUCCESS;
    HANDLE fileHandle = INVALID_HANDLE_VALUE;
    WORD bitmapHeader = 0;
    DWORD pixelDataOffset = 0;
    DWORD numberOfBytesRead = 0;

    if ((fileHandle = CreateFileA(fileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        goto Exit;
    }

    if (ReadFile(fileHandle, &bitmapHeader, 2, &numberOfBytesRead, NULL) == 0) {
        error = GetLastError();
        goto Exit;
    }

    if (bitmapHeader != 0x4d42) { // "BM" backwords
        error = ERROR_FILE_INVALID;
        goto Exit;
    }

    if (SetFilePointer(fileHandle, 10, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
        error = GetLastError();
        goto Exit;
    }

    if (ReadFile(fileHandle, &pixelDataOffset, sizeof(DWORD), &numberOfBytesRead, NULL) == 0) {
        error = GetLastError();
        goto Exit;
    }

    if (SetFilePointer(fileHandle, 0xe, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
        error = GetLastError();
        goto Exit;
    }

    if (ReadFile(fileHandle, &gameBitmap->bitmapinfo.bmiHeader, sizeof(BITMAPINFOHEADER), &numberOfBytesRead, NULL) == 0) {
        error = GetLastError();
        goto Exit;
    }

    if ((gameBitmap->memory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, gameBitmap->bitmapinfo.bmiHeader.biSizeImage)) == NULL) {
        error = ERROR_NOT_ENOUGH_MEMORY;
        goto Exit;
    }

    if (SetFilePointer(fileHandle, pixelDataOffset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
        error = GetLastError();
        goto Exit;
    }

    if (ReadFile(fileHandle, gameBitmap->memory, gameBitmap->bitmapinfo.bmiHeader.biSizeImage, &numberOfBytesRead, NULL) == 0) {
        error = GetLastError();
        goto Exit;
    }

Exit:
    if (fileHandle && (fileHandle != INVALID_HANDLE_VALUE)) {
        CloseHandle(fileHandle);
    }
    
    LogMessageA(LOG_ERROR, "[%s] Error 0x%08lx!", __FUNCTION__, error);

    return error;
}

DWORD InitializeHero(void) {
    DWORD error = ERROR_SUCCESS;

    gPlayer.screenPosX = 32;
    gPlayer.screenPosY = 32;
    gPlayer.currentArmor = SUIT_0;
    gPlayer.direction = DOWN;

    if ((error = Load32BppBitmapFromFile("..\\Assets\\Hero_Suit0_Down_Standing.bmpx", &gPlayer.sprite[SUIT_0][FACING_DOWN_0])) != ERROR_SUCCESS) {
        MessageBoxA(NULL, "Failed to load Hero_Suit0_Down_Standing!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        goto Exit;
    }

    if ((error = Load32BppBitmapFromFile("..\\Assets\\Hero_Suit0_Down_Walk1.bmpx", &gPlayer.sprite[SUIT_0][FACING_DOWN_1])) != ERROR_SUCCESS) {
        MessageBoxA(NULL, "Failed to load Hero_Suit0_Down_Walk1!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        goto Exit;
    }

    if ((error = Load32BppBitmapFromFile("..\\Assets\\Hero_Suit0_Down_Walk2.bmpx", &gPlayer.sprite[SUIT_0][FACING_DOWN_2])) != ERROR_SUCCESS) {
        MessageBoxA(NULL, "Failed to load Hero_Suit0_Down_Walk2!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        goto Exit;
    }

    if ((error = Load32BppBitmapFromFile("..\\Assets\\Hero_Suit0_Left_Standing.bmpx", &gPlayer.sprite[SUIT_0][FACING_LEFT_0])) != ERROR_SUCCESS) {
        MessageBoxA(NULL, "Failed to load Hero_Suit0_Left_Standing.bmpx!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        goto Exit;
    }

    if ((error = Load32BppBitmapFromFile("..\\Assets\\Hero_Suit0_Left_Walk1.bmpx", &gPlayer.sprite[SUIT_0][FACING_LEFT_1])) != ERROR_SUCCESS) {
        MessageBoxA(NULL, "Failed to load Hero_Suit0_Left_Walk1!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        goto Exit;
    }

    if ((error = Load32BppBitmapFromFile("..\\Assets\\Hero_Suit0_Left_Walk2.bmpx", &gPlayer.sprite[SUIT_0][FACING_LEFT_2])) != ERROR_SUCCESS) {
        MessageBoxA(NULL, "Failed to load Hero_Suit0_Left_Walk2!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        goto Exit;
    }

    if ((error = Load32BppBitmapFromFile("..\\Assets\\Hero_Suit0_Right_Standing.bmpx", &gPlayer.sprite[SUIT_0][FACING_RIGHT_0])) != ERROR_SUCCESS) {
        MessageBoxA(NULL, "Failed to load Hero_Suit0_Right_Standing.bmpx!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        goto Exit;
    }

    if ((error = Load32BppBitmapFromFile("..\\Assets\\Hero_Suit0_Right_Walk1.bmpx", &gPlayer.sprite[SUIT_0][FACING_RIGHT_1])) != ERROR_SUCCESS) {
        MessageBoxA(NULL, "Failed to load Hero_Suit0_Right_Walk1!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        goto Exit;
    }

    if ((error = Load32BppBitmapFromFile("..\\Assets\\Hero_Suit0_Right_Walk2.bmpx", &gPlayer.sprite[SUIT_0][FACING_RIGHT_2])) != ERROR_SUCCESS) {
        MessageBoxA(NULL, "Failed to load Hero_Suit0_Right_Walk2!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        goto Exit;
    }

    if ((error = Load32BppBitmapFromFile("..\\Assets\\Hero_Suit0_Up_Standing.bmpx", &gPlayer.sprite[SUIT_0][FACING_UP_0])) != ERROR_SUCCESS) {
        MessageBoxA(NULL, "Failed to load Hero_Suit0_Up_Standing.bmpx!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        goto Exit;
    }

    if ((error = Load32BppBitmapFromFile("..\\Assets\\Hero_Suit0_Up_Walk1.bmpx", &gPlayer.sprite[SUIT_0][FACING_UP_1])) != ERROR_SUCCESS) {
        MessageBoxA(NULL, "Failed to load Hero_Suit0_Up_Walk1!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        goto Exit;
    }

    if ((error = Load32BppBitmapFromFile("..\\Assets\\Hero_Suit0_Up_Walk2.bmpx", &gPlayer.sprite[SUIT_0][FACING_UP_2])) != ERROR_SUCCESS) {
        MessageBoxA(NULL, "Failed to load Hero_Suit0_Up_Walk2!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        goto Exit;
    }

Exit:
    return error;
}

void Blit32BppBitmapToBuffer(_In_ GAMEBITMAP* gameBitmap, _In_ uint16_t x, _In_ uint16_t y) {
    int32_t startingScreenPixel = ((GAME_RES_WIDTH * GAME_RES_HEIGHT) - GAME_RES_WIDTH) - (GAME_RES_WIDTH * y) + x;

    int32_t startingBitmapPixel = ((gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth);

    int32_t memoryOffset = 0;
    int32_t bitmapOffset = 0;
    PIXEL32 bitmapPixel = { 0 };
    // PIXEL32 backgroundPixel = { 0 };

    for (int16_t yPixel = 0; yPixel < gameBitmap->bitmapinfo.bmiHeader.biHeight; yPixel++) {
        for (int16_t xPixel = 0; xPixel < gameBitmap->bitmapinfo.bmiHeader.biWidth; xPixel++) {
            memoryOffset = startingScreenPixel + xPixel - (GAME_RES_WIDTH * yPixel);
            bitmapOffset = startingBitmapPixel + xPixel - (gameBitmap->bitmapinfo.bmiHeader.biWidth * yPixel);

            memcpy_s(&bitmapPixel, sizeof(PIXEL32), (PIXEL32*)gameBitmap->memory + bitmapOffset, sizeof(PIXEL32));

            if (bitmapPixel.alpha == 255) {
                memcpy_s((PIXEL32*)gBackBuffer.memory + memoryOffset, sizeof(PIXEL32), &bitmapPixel, sizeof(PIXEL32));
            }
        }
    }
}

void BlitStringToBuffer(_In_ char* string, _In_ GAMEBITMAP* fontSheet, _In_ PIXEL32* color, _In_ uint16_t x, _In_ uint16_t y) {
    uint16_t charWidth = (uint16_t)fontSheet->bitmapinfo.bmiHeader.biWidth / FONT_SHEET_CHARACTERS_PER_ROW;
    uint16_t charHeight = (uint16_t)fontSheet->bitmapinfo.bmiHeader.biHeight;
    uint16_t bytesPerCharacter = (charWidth * charHeight * (fontSheet->bitmapinfo.bmiHeader.biBitCount / 8));
    uint16_t stringLength = (uint16_t)strlen(string);

    GAMEBITMAP stringBitmap = { 0 };

    stringBitmap.bitmapinfo.bmiHeader.biBitCount = GAME_BPP;
    stringBitmap.bitmapinfo.bmiHeader.biHeight = charHeight;
    stringBitmap.bitmapinfo.bmiHeader.biWidth = charWidth * stringLength;
    stringBitmap.bitmapinfo.bmiHeader.biPlanes = 1;
    stringBitmap.bitmapinfo.bmiHeader.biCompression = BI_RGB;

    stringBitmap.memory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)bytesPerCharacter * (size_t)stringLength);

    for (int character = 0; character < stringLength; character++) {
        int startingFontSheetPixel = 0;
        int fontSheetOffset = 0;
        int stringBitmapOffset = 0;
        PIXEL32 fontSheetPixel = { 0 };

        switch (string[character]) {
        case 'A':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth;
            break;
        case 'B':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + charWidth;
            break;
        case 'C':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (2 * charWidth);
            break;
        case 'D':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (3 * charWidth);
            break;
        case 'E':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (4 * charWidth);
            break;
        case 'F':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (5 * charWidth);
            break;
        case 'G':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (6 * charWidth);
            break;
        case 'H':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (7 * charWidth);
            break;
        case 'I':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (8 * charWidth);
            break;
        case 'J':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (9 * charWidth);
            break;
        case 'K':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (10 * charWidth);
            break;
        case 'L':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (11 * charWidth);
            break;
        case 'M':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (12 * charWidth);
            break;
        case 'N':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (13 * charWidth);
            break;
        case 'O':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (14 * charWidth);
            break;
        case 'P':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (15 * charWidth);
            break;
        case 'Q':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (16 * charWidth);
            break;
        case 'R':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (17 * charWidth);
            break;
        case 'S':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (18 * charWidth);
            break;
        case 'T':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (19 * charWidth);
            break;
        case 'U':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (20 * charWidth);
            break;
        case 'V':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (21 * charWidth);
            break;
        case 'W':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (22 * charWidth);
            break;
        case 'X':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (23 * charWidth);
            break;
        case 'Y':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (24 * charWidth);
            break;
        case 'Z':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (25 * charWidth);
            break;
        case 'a':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (26 * charWidth);
            break;
        case 'b':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (27 * charWidth);
            break;
        case 'c':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (28 * charWidth);
            break;
        case 'd':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (29 * charWidth);
            break;
        case 'e':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (30 * charWidth);
            break;
        case 'f':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (31 * charWidth);
            break;
        case 'g':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (32 * charWidth);
            break;
        case 'h':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (33 * charWidth);
            break;
        case 'i':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (34 * charWidth);
            break;
        case 'j':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (35 * charWidth);
            break;
        case 'k':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (36 * charWidth);
            break;
        case 'l':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (37 * charWidth);
            break;
        case 'm':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (38 * charWidth);
            break;
        case 'n':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (39 * charWidth);
            break;
        case 'o':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (40 * charWidth);
            break;
        case 'p':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (41 * charWidth);
            break;
        case 'q':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (42 * charWidth);
            break;
        case 'r':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (43 * charWidth);
            break;
        case 's':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (44 * charWidth);
            break;
        case 't':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (45 * charWidth);
            break;
        case 'u':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (46 * charWidth);
            break;
        case 'v':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (47 * charWidth);
            break;
        case 'w':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (48 * charWidth);
            break;
        case 'x':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (49 * charWidth);
            break;
        case 'y':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (50 * charWidth);
            break;
        case 'z':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (51 * charWidth);
            break;
        case '0':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (52 * charWidth);
            break;
        case '1':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (53 * charWidth);
            break;
        case '2':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (54 * charWidth);
            break;
        case '3':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (55 * charWidth);
            break;
        case '4':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (56 * charWidth);
            break;
        case '5':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (57 * charWidth);
            break;
        case '6':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (58 * charWidth);
            break;
        case '7':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (59 * charWidth);
            break;
        case '8':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (60 * charWidth);
            break;
        case '9':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (61 * charWidth);
            break;
        case '`':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (62 * charWidth);
            break;
        case '~':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (63 * charWidth);
            break;
        case '!':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (64 * charWidth);
            break;
        case '@':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (65 * charWidth);
            break;
        case '#':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (66 * charWidth);
            break;
        case '$':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (67 * charWidth);
            break;
        case '%':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (68 * charWidth);
            break;
        case '^':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (69 * charWidth);
            break;
        case '&':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (70 * charWidth);
            break;
        case '*':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (71 * charWidth);
            break;
        case '(':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (72 * charWidth);
            break;
        case ')':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (73 * charWidth);
            break;
        case '-':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (74 * charWidth);
            break;
        case '=':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (75 * charWidth);
            break;
        case '_':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (76 * charWidth);
            break;
        case '+':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (77 * charWidth);
            break;
        case '\\':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (78 * charWidth);
            break;
        case '|':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (79 * charWidth);
            break;
        case '[':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (80 * charWidth);
            break;
        case ']':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (81 * charWidth);
            break;
        case '{':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (82 * charWidth);
            break;
        case '}':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (83 * charWidth);
            break;
        case ';':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (84 * charWidth);
            break;
        case '\'':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (85 * charWidth);
            break;
        case ':':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (86 * charWidth);
            break;
        case '"':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (87 * charWidth);
            break;
        case ',':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (88 * charWidth);
            break;
        case '<':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (89 * charWidth);
            break;
        case '>':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (90 * charWidth);
            break;
        case '.':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (91 * charWidth);
            break;
        case '/':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (92 * charWidth);
            break;
        case '?':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (93 * charWidth);
            break;
        case ' ':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (94 * charWidth);
            break;
        // Github does not preserve this character ASCII 0xBB
        case '»':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (95 * charWidth);
            break;
        // Github does not preserve this character ASCII 0xAB
        case '«':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (96 * charWidth);
            break;
        case '\xf2':
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (97 * charWidth);
            break;
        default:
            startingFontSheetPixel = (fontSheet->bitmapinfo.bmiHeader.biWidth * fontSheet->bitmapinfo.bmiHeader.biHeight) - fontSheet->bitmapinfo.bmiHeader.biWidth + (93 * charWidth);
        }

        for (int yPixel = 0; yPixel < charHeight; yPixel++) {
            for (int xPixel = 0; xPixel < charWidth; xPixel++) {
                fontSheetOffset = startingFontSheetPixel + xPixel - (fontSheet->bitmapinfo.bmiHeader.biWidth * yPixel);
                stringBitmapOffset = (character * charWidth) + (stringBitmap.bitmapinfo.bmiHeader.biWidth * stringBitmap.bitmapinfo.bmiHeader.biHeight) -
                    stringBitmap.bitmapinfo.bmiHeader.biWidth + xPixel - (stringBitmap.bitmapinfo.bmiHeader.biWidth * yPixel);

                memcpy_s(&fontSheetPixel, sizeof(PIXEL32), (PIXEL32*)fontSheet->memory + fontSheetOffset, sizeof(PIXEL32));

                fontSheetPixel.red = color->red;
                fontSheetPixel.green = color->green;
                fontSheetPixel.blue = color->blue;

                memcpy_s((PIXEL32*)stringBitmap.memory + stringBitmapOffset, sizeof(PIXEL32), &fontSheetPixel, sizeof(PIXEL32));
            }
        }
    }

    Blit32BppBitmapToBuffer(&stringBitmap, x, y);

    if (stringBitmap.memory) {
        HeapFree(GetProcessHeap(), 0, stringBitmap.memory);
    }
}

void RenderFrameGraphics(void) {
#ifdef SIMD
    __m128i quadPixel = { 0x7f, 0x00, 0x00, 0xff, 0x7f, 0x00, 0x00, 0xff, 0x7f, 0x00, 0x00, 0xff, 0x7f, 0x00, 0x00, 0xff };

    ClearScreen(&quadPixel);
#else
    PIXEL32 pixel = { 0x7f, 0x00, 0x00, 0xff };

    ClearScreen(&pixel);
#endif

    Blit32BppBitmapToBuffer(&gPlayer.sprite[gPlayer.currentArmor][gPlayer.direction + gPlayer.spriteIndex], gPlayer.screenPosX, gPlayer.screenPosY);

    

    if (gPerformanceData.displayDebugInfo) {
        DrawDebugInfo();
    }

    HDC deviceContext = GetDC(gGameWindow);

    StretchDIBits(deviceContext, 0, 0, gPerformanceData.monitorWidth, gPerformanceData.monitorHeight, 0, 0,
        GAME_RES_WIDTH, GAME_RES_HEIGHT, gBackBuffer.memory, &gBackBuffer.bitmapinfo, DIB_RGB_COLORS, SRCCOPY);

    ReleaseDC(gGameWindow, deviceContext);
}

#ifdef SIMD
__forceinline void ClearScreen(_In_ __m128i* color) {
    for (int i = 0; i < GAME_RES_WIDTH * GAME_RES_HEIGHT; i += 4) {
        _mm_store_si128((PIXEL32*)gBackBuffer.memory + i, *color);
    }
}
#else
__forceinline void ClearScreen(_In_ PIXEL32* pixel) {
    for (int i = 0; i < GAME_RES_WIDTH * GAME_RES_HEIGHT; i++) {
        memcpy((PIXEL32*)gBackBuffer.memory + i, pixel, sizeof(PIXEL32));
    }
}
#endif

DWORD LoadRegistryParameters(void) {
    DWORD result = ERROR_SUCCESS;
    HKEY regKey = NULL;
    DWORD regDisposotion = 0;
    DWORD regBytesRead = sizeof(DWORD);
    result = RegCreateKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\" GAME_NAME, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &regKey, &regDisposotion);

    if (result != ERROR_SUCCESS) {
        LogMessageA(LOG_ERROR, "[%s] RegCreateKey failed with error code 0x%08lx!", __FUNCTION__, result);
        goto Exit;
    }

    if (regDisposotion == REG_CREATED_NEW_KEY) {
        LogMessageA(LOG_ERROR, "[%s] Register key did not exist; created new key HKCU\\SOFTWARE\\%s", __FUNCTION__, GAME_NAME);
    }
    else {
        LogMessageA(LOG_INFO, "[%s] Open existing registry key HKCU\\SOFTWARE\\%s", __FUNCTION__, GAME_NAME);
    }

    result = RegGetValueA(regKey, NULL, "LogLevel", RRF_RT_DWORD, NULL, (BYTE*)&gRegistryParams.logLevel, &regBytesRead);

    if (result != ERROR_SUCCESS) {
        if (result == ERROR_FILE_NOT_FOUND) {
            result = ERROR_SUCCESS;
            LogMessageA(LOG_INFO, "[%s] Registry value 'LogLevel' not found. Using default of 0. (LOG_LEVEL_NONE)", __FUNCTION__);
            gRegistryParams.logLevel = LOG_NONE;
        }
        else {
            LogMessageA(LOG_ERROR, "[%s] Failed to read the 'LogLevel' registry value! Error 0x%08lx!", __FUNCTION__, result);
        }
    }

    LogMessageA(LOG_INFO, "[%s] LogLevel is %d.", __FUNCTION__, gRegistryParams.logLevel);

    //.....

Exit:

    if (regKey) {
        RegCloseKey(regKey);
    }

    return result;
}

void LogMessageA(_In_ DWORD logLevel, _In_ char* message, _In_ ...) {
    size_t messageLength = strlen(message);
    SYSTEMTIME time = { 0 };
    HANDLE logFileHandle = INVALID_HANDLE_VALUE;
    DWORD endOfFile = 0;
    DWORD numberOfBytesWritten = 0;
    char dateTimeString[96] = { 0 };
    char severityString[8] = { 0 };
    char formattedString[4096] = { 0 };
    int error = 0;

    if (gRegistryParams.logLevel < logLevel) {
        return;
    }

    if (messageLength < 1 || messageLength > 4096) {
        // assert?
        return;
    }

    switch (logLevel) {
        case LOG_NONE:
            return;
        case LOG_INFO:
            strcpy_s(severityString, sizeof(severityString), "[INFO]");
            break;
        case LOG_WARN:
            strcpy_s(severityString, sizeof(severityString), "[WARN]");
            break;
        case LOG_ERROR:
            strcpy_s(severityString, sizeof(severityString), "[ERROR]");
            break;
        case LOG_DEBUG:
            strcpy_s(severityString, sizeof(severityString), "[DEBUG]");
            break;
        default:
            ASSERT(FALSE);
    }

    GetLocalTime(&time);

    va_list argPointer = NULL;
    va_start(argPointer, message);
    _vsnprintf_s(formattedString, sizeof(formattedString), _TRUNCATE, message, argPointer);
    va_end(argPointer);

    error = _snprintf_s(dateTimeString, sizeof(dateTimeString), _TRUNCATE,
        "\r\n[%02u/%02u/%u %02u:%02u.%03u]", time.wDay, time.wMonth, time.wYear, time.wHour, time.wMinute, time.wSecond);

    if ((logFileHandle = CreateFileA(LOG_FILE_NAME, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE) {
        // assert?
        return;
    }

    endOfFile = SetFilePointer(logFileHandle, 0, NULL, FILE_END);

    WriteFile(logFileHandle, dateTimeString, (DWORD)strlen(dateTimeString), &numberOfBytesWritten, NULL);
    WriteFile(logFileHandle, severityString, (DWORD)strlen(severityString), &numberOfBytesWritten, NULL);
    WriteFile(logFileHandle, formattedString, (DWORD)strlen(formattedString), &numberOfBytesWritten, NULL);

    if (logFileHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(logFileHandle);
    }
}

void DrawDebugInfo(void) {
    char debugTextBuffer[64] = { 0 };
    PIXEL32 white = { 0xff, 0xff, 0xff, 0xff };

    sprintf_s(debugTextBuffer, sizeof(debugTextBuffer), "FPS Raw:        %.01f", gPerformanceData.rawFPSAverage);
    BlitStringToBuffer(&debugTextBuffer, &g6x7Font, &white, 0, 0);
    
    sprintf_s(debugTextBuffer, sizeof(debugTextBuffer), "FPS Cooked:     %.01f  ", gPerformanceData.cookedFPSAverage);
    BlitStringToBuffer(&debugTextBuffer, &g6x7Font, &white, 0, 8);

    sprintf_s(debugTextBuffer, sizeof(debugTextBuffer), "Min. Timer Res: %.02f ", gPerformanceData.minimumTimerResolution / 10000.0f);
    BlitStringToBuffer(&debugTextBuffer, &g6x7Font, &white, 0, 16);

    sprintf_s(debugTextBuffer, sizeof(debugTextBuffer), "Max. Timer Res: %.02f  ", gPerformanceData.maximumTimerResolution / 10000.0f);
    BlitStringToBuffer(&debugTextBuffer, &g6x7Font, &white, 0, 24);

    sprintf_s(debugTextBuffer, sizeof(debugTextBuffer), "Cur. Timer Res: %.02f  ", gPerformanceData.currentTimerResolution / 10000.0f);
    BlitStringToBuffer(&debugTextBuffer, &g6x7Font, &white, 0, 32);

    sprintf_s(debugTextBuffer, sizeof(debugTextBuffer), "Handles:        %lu   ", gPerformanceData.handleCount);
    BlitStringToBuffer(&debugTextBuffer, &g6x7Font, &white, 0, 40);

    sprintf_s(debugTextBuffer, sizeof(debugTextBuffer), "Memory:         %lu KB ", gPerformanceData.memInfo.PrivateUsage / 1024);
    BlitStringToBuffer(&debugTextBuffer, &g6x7Font, &white, 0, 48);

    sprintf_s(debugTextBuffer, sizeof(debugTextBuffer), "CPU:            %.02f %% ", gPerformanceData.cpuPercent);
    BlitStringToBuffer(&debugTextBuffer, &g6x7Font, &white, 0, 56);

    sprintf_s(debugTextBuffer, sizeof(debugTextBuffer), "Total Frames:   %llu ", gPerformanceData.totalFramesRendered);
    BlitStringToBuffer(&debugTextBuffer, &g6x7Font, &white, 0, 64);

    sprintf_s(debugTextBuffer, sizeof(debugTextBuffer), "Screen Pos:     (%d,%d)", gPlayer.screenPosX, gPlayer.screenPosY);
    BlitStringToBuffer(&debugTextBuffer, &g6x7Font, &white, 0, 72);
}