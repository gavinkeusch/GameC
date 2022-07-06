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

    if ((Load32BppBitmapFromFile("..\\Assets\\6x7Font.bmpx", &g6x7Font)) != ERROR_SUCCESS) {
        MessageBoxA(NULL, "Failed to load 6x7Font!", "Error!", MB_ICONEXCLAMATION | MB_OK);
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

    if (InitializeHero() != ERROR_SUCCESS) {
        MessageBoxA(NULL, "Failed to initialize hero!", "Error!", MB_ICONEXCLAMATION | MB_OK);
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

    if (!gPlayer.movementRemaining) {
        if (downKeyIsDown) {
            if (gPlayer.screenPosY < GAME_RES_HEIGHT - 16) {
                gPlayer.direction = DIRECTION_DOWN;
                gPlayer.movementRemaining = 16;
            }
        } 
        else if (leftKeyIsDown) {
            if (gPlayer.screenPosY < GAME_RES_HEIGHT - 16) {
                gPlayer.direction = DIRECTION_LEFT;
                gPlayer.movementRemaining = 16;
            }
        } 
        else if (rightKeyIsDown) {
            if (gPlayer.screenPosX < GAME_RES_WIDTH - 16) {
                gPlayer.direction = DIRECTION_RIGHT;
                gPlayer.movementRemaining = 16;
            }
        } 
        else if (upKeyIsDown) {
            if (gPlayer.screenPosY > 0) {
                gPlayer.direction = DIRECTION_UP;
                gPlayer.movementRemaining = 16;
            }
        }
    }
    else {
        gPlayer.movementRemaining--;

        if (gPlayer.direction == DIRECTION_DOWN) {
            gPlayer.screenPosY++;
        }
        else if (gPlayer.direction == DIRECTION_LEFT) {
            gPlayer.screenPosX--;
        }
        else if (gPlayer.direction == DIRECTION_RIGHT) {
            gPlayer.screenPosX++;
        }
        else if (gPlayer.direction == DIRECTION_UP) {
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
            default:
                // assert
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
    
    return error;
}

DWORD InitializeHero(void) {
    DWORD error = ERROR_SUCCESS;

    gPlayer.screenPosX = 32;
    gPlayer.screenPosY = 32;
    gPlayer.currentArmor = SUIT_0;
    gPlayer.direction = DIRECTION_DOWN;

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
    PIXEL32 backgroundPixel = { 0 };

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

void BlitStringToBuffer(_In_ char* string, _In_ GAMEBITMAP* gameBitmap, _In_ uint16_t x, _In_ uint16_t y) {
    int charWidth = gameBitmap->bitmapinfo.bmiHeader.biWidth / FONT_SHEET_CHARACTERS_PER_ROW;
    int charHeight = gameBitmap->bitmapinfo.bmiHeader.biHeight;
    int bytesPerCharacter = (charWidth * charHeight * (gameBitmap->bitmapinfo.bmiHeader.biBitCount / 8));
    int stringLength = strlen(string);

    GAMEBITMAP stringBitmap = { 0 };

    stringBitmap.bitmapinfo.bmiHeader.biBitCount = GAME_BPP;
    stringBitmap.bitmapinfo.bmiHeader.biHeight = charHeight;
    stringBitmap.bitmapinfo.bmiHeader.biWidth = charWidth * stringLength;
    stringBitmap.bitmapinfo.bmiHeader.biPlanes = 1;
    stringBitmap.bitmapinfo.bmiHeader.biCompression = BI_RGB;

    stringBitmap.memory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytesPerCharacter * stringLength);

    for (int character = 0; character < stringLength; character++) {
        int startingFontSheetByte = 0;
        int fontSheetOffset = 0;
        int stringBitmapOffset = 0;
        PIXEL32 fontSheetPixel = { 0 };

        switch (string[character]) {
        case 'A':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth;
            break;
        case 'B':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + charWidth;
            break;
        case 'C':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (2 * charWidth);
            break;
        case 'D':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (3 * charWidth);
            break;
        case 'E':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (4 * charWidth);
            break;
        case 'F':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (5 * charWidth);
            break;
        case 'G':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (6 * charWidth);
            break;
        case 'H':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (7 * charWidth);
            break;
        case 'I':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (8 * charWidth);
            break;
        case 'J':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (9 * charWidth);
            break;
        case 'K':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (10 * charWidth);
            break;
        case 'L':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (11 * charWidth);
            break;
        case 'M':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (12 * charWidth);
            break;
        case 'N':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (13 * charWidth);
            break;
        case 'O':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (14 * charWidth);
            break;
        case 'P':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (15 * charWidth);
            break;
        case 'Q':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (16 * charWidth);
            break;
        case 'R':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (17 * charWidth);
            break;
        case 'S':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (18 * charWidth);
            break;
        case 'T':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (19 * charWidth);
            break;
        case 'U':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (20 * charWidth);
            break;
        case 'V':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (21 * charWidth);
            break;
        case 'W':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (22 * charWidth);
            break;
        case 'X':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (23 * charWidth);
            break;
        case 'Y':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (24 * charWidth);
            break;
        case 'Z':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (25 * charWidth);
            break;
        case 'a':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (26 * charWidth);
            break;
        case 'b':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (27 * charWidth);
            break;
        case 'c':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (28 * charWidth);
            break;
        case 'd':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (29 * charWidth);
            break;
        case 'e':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (30 * charWidth);
            break;
        case 'f':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (31 * charWidth);
            break;
        case 'g':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (32 * charWidth);
            break;
        case 'h':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (33 * charWidth);
            break;
        case 'i':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (34 * charWidth);
            break;
        case 'j':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (35 * charWidth);
            break;
        case 'k':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (36 * charWidth);
            break;
        case 'l':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (37 * charWidth);
            break;
        case 'm':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (38 * charWidth);
            break;
        case 'n':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (39 * charWidth);
            break;
        case 'o':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (40 * charWidth);
            break;
        case 'p':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (41 * charWidth);
            break;
        case 'q':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (42 * charWidth);
            break;
        case 'r':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (43 * charWidth);
            break;
        case 's':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (44 * charWidth);
            break;
        case 't':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (45 * charWidth);
            break;
        case 'u':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (46 * charWidth);
            break;
        case 'v':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (47 * charWidth);
            break;
        case 'w':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (48 * charWidth);
            break;
        case 'x':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (49 * charWidth);
            break;
        case 'y':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (50 * charWidth);
            break;
        case 'z':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (51 * charWidth);
            break;
        case '0':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (52 * charWidth);
            break;
        case '1':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (53 * charWidth);
            break;
        case '2':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (54 * charWidth);
            break;
        case '3':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (55 * charWidth);
            break;
        case '4':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (56 * charWidth);
            break;
        case '5':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (57 * charWidth);
            break;
        case '6':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (58 * charWidth);
            break;
        case '7':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (59 * charWidth);
            break;
        case '8':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (60 * charWidth);
            break;
        case '9':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (61 * charWidth);
            break;
        case '`':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (62 * charWidth);
            break;
        case '~':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (63 * charWidth);
            break;
        case '!':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (64 * charWidth);
            break;
        case '@':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (65 * charWidth);
            break;
        case '#':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (66 * charWidth);
            break;
        case '$':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (67 * charWidth);
            break;
        case '%':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (68 * charWidth);
            break;
        case '^':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (69 * charWidth);
            break;
        case '&':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (70 * charWidth);
            break;
        case '*':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (71 * charWidth);
            break;
        case '(':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (72 * charWidth);
            break;
        case ')':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (73 * charWidth);
            break;
        case '-':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (74 * charWidth);
            break;
        case '=':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (75 * charWidth);
            break;
        case '_':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (76 * charWidth);
            break;
        case '+':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (77 * charWidth);
            break;
        case '\\':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (78 * charWidth);
            break;
        case '|':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (79 * charWidth);
            break;
        case '[':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (80 * charWidth);
            break;
        case ']':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (81 * charWidth);
            break;
        case '{':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (82 * charWidth);
            break;
        case '}':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (83 * charWidth);
            break;
        case ';':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (84 * charWidth);
            break;
        case '\'':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (85 * charWidth);
            break;
        case ':':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (86 * charWidth);
            break;
        case '"':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (87 * charWidth);
            break;
        case ',':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (88 * charWidth);
            break;
        case '<':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (89 * charWidth);
            break;
        case '>':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (90 * charWidth);
            break;
        case '.':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (91 * charWidth);
            break;
        case '/':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (92 * charWidth);
            break;
        case '?':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (93 * charWidth);
            break;
        case ' ':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (94 * charWidth);
            break;
        // Github does not preserve this character ASCII 0xBB
        case '»':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (95 * charWidth);
            break;
        // Github does not preserve this character ASCII 0xAB
        case '«':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (96 * charWidth);
            break;
        case '\xf2':
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (97 * charWidth);
            break;
        default:
            // TODO: assert?
            startingFontSheetByte = (gameBitmap->bitmapinfo.bmiHeader.biWidth * gameBitmap->bitmapinfo.bmiHeader.biHeight) - gameBitmap->bitmapinfo.bmiHeader.biWidth + (93 * charWidth);

        }

        for (int yPixel = 0; yPixel < charHeight; yPixel++) {
            for (int xPixel = 0; xPixel < charWidth; xPixel++) {
                fontSheetOffset = startingFontSheetByte + xPixel - (gameBitmap->bitmapinfo.bmiHeader.biWidth * yPixel);
                stringBitmapOffset = (character * charWidth) + (stringBitmap.bitmapinfo.bmiHeader.biWidth * stringBitmap.bitmapinfo.bmiHeader.biHeight) -
                    stringBitmap.bitmapinfo.bmiHeader.biWidth + xPixel - (stringBitmap.bitmapinfo.bmiHeader.biWidth * yPixel);

                memcpy_s(&fontSheetPixel, sizeof(PIXEL32), (PIXEL32*)gameBitmap->memory + fontSheetOffset, sizeof(PIXEL32));

                fontSheetPixel.red = 0xff;
                fontSheetPixel.green = 0.00;
                fontSheetPixel.blue = 0x00;

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

    BlitStringToBuffer(">>GAME OVER<<", &g6x7Font, 60, 60);

    Blit32BppBitmapToBuffer(&gPlayer.sprite[gPlayer.currentArmor][gPlayer.direction + gPlayer.spriteIndex], gPlayer.screenPosX, gPlayer.screenPosY);

    HDC deviceContext = GetDC(gGameWindow);

    StretchDIBits(deviceContext, 0, 0, gPerformanceData.monitorWidth, gPerformanceData.monitorHeight, 0,0,
                  GAME_RES_WIDTH,GAME_RES_HEIGHT, gBackBuffer.memory, &gBackBuffer.bitmapinfo, DIB_RGB_COLORS, SRCCOPY);

    if (gPerformanceData.displayDebugInfo) {
        SelectObject(deviceContext, (HFONT) GetStockObject(ANSI_FIXED_FONT));
        char DebugTextBuffer[64] = { 0 };
        sprintf_s(DebugTextBuffer, sizeof(DebugTextBuffer), "FPS Raw:        %.01f", gPerformanceData.rawFPSAverage);
        TextOutA(deviceContext,0 , 0, DebugTextBuffer, (int)strlen(DebugTextBuffer));
        sprintf_s(DebugTextBuffer, sizeof(DebugTextBuffer), "FPS Cooked:     %.01f  ", gPerformanceData.cookedFPSAverage);
        TextOutA(deviceContext,0 , 13, DebugTextBuffer, (int)strlen(DebugTextBuffer));

        sprintf_s(DebugTextBuffer, sizeof(DebugTextBuffer), "Min. Timer Res: %.02f ", gPerformanceData.minimumTimerResolution / 10000.0f);
        TextOutA(deviceContext,0 , 26, DebugTextBuffer, (int)strlen(DebugTextBuffer));
        sprintf_s(DebugTextBuffer, sizeof(DebugTextBuffer), "Max. Timer Res: %.02f  ", gPerformanceData.maximumTimerResolution / 10000.0f);
        TextOutA(deviceContext,0 , 39, DebugTextBuffer, (int)strlen(DebugTextBuffer));
        sprintf_s(DebugTextBuffer, sizeof(DebugTextBuffer), "Cur. Timer Res: %.02f  ", gPerformanceData.currentTimerResolution / 10000.0f);
        TextOutA(deviceContext,0 , 52, DebugTextBuffer, (int)strlen(DebugTextBuffer));

        sprintf_s(DebugTextBuffer, sizeof(DebugTextBuffer), "Handles:        %lu   ", gPerformanceData.handleCount);
        TextOutA(deviceContext,0 , 65, DebugTextBuffer, (int)strlen(DebugTextBuffer));
        sprintf_s(DebugTextBuffer, sizeof(DebugTextBuffer), "Memory:       %lu KB ", gPerformanceData.memInfo.PrivateUsage / 1024);
        TextOutA(deviceContext,0 , 78, DebugTextBuffer, (int)strlen(DebugTextBuffer));

        sprintf_s(DebugTextBuffer, sizeof(DebugTextBuffer), "CPU:           %.02f %% ", gPerformanceData.cpuPercent);
        TextOutA(deviceContext,0 , 91, DebugTextBuffer, (int)strlen(DebugTextBuffer));

        sprintf_s(DebugTextBuffer, sizeof(DebugTextBuffer), "Total Frames:   %llu ", gPerformanceData.totalFramesRendered);
        TextOutA(deviceContext, 0, 104, DebugTextBuffer, (int)strlen(DebugTextBuffer));

        sprintf_s(DebugTextBuffer, sizeof(DebugTextBuffer), "Screen Pos:  (%d,%d)", gPlayer.screenPosX, gPlayer.screenPosY);
        TextOutA(deviceContext, 0, 117, DebugTextBuffer, (int)strlen(DebugTextBuffer));
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