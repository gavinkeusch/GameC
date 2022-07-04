#pragma once

#define GAME_NAME       "Game_C"
#define GAME_RES_WIDTH  384
#define GAME_RES_HEIGHT 240
#define GAME_BPP        32
#define GAME_DRAWING_AREA_MEMORY_SIZE (GAME_RES_WIDTH * GAME_RES_HEIGHT * (GAME_BPP / 8))
#define CALCULATE_AVG_FPS_EVERY_X_FRAMES 120
#define TARGET_MICROSECONDS_PER_FRAME 16667ULL
//#define SIMD

#pragma warning(disable: 4820) // disable warning about structure padding
#pragma warning(disable: 5045) // disable warning about spectre/meltdown CPU vulnerability
#pragma warning(disable: 4710) // disable warning about function not inlined

typedef LONG(NTAPI* _NtQueryTimerResolution) (OUT PULONG MinimumResolution, OUT PULONG MaximumResolution, OUT PULONG CurrentResolution);
_NtQueryTimerResolution NtQueryTimerResolution;

typedef struct GAMEBITMAP {
    BITMAPINFO bitmapinfo;
    void* memory;
} GAMEBITMAP;

typedef struct PIXEL32 {
    uint8_t blue;
    uint8_t green;
    uint8_t red;
    uint8_t alpha;
} PIXEL32;

typedef struct GAMEPERFORMANCEDATA {
    uint64_t totalFramesRendered;
    float rawFPSAverage;
    float cookedFPSAverage;
    int64_t perfFrequency;
    MONITORINFO monitorInfo;
    int32_t monitorWidth;
    int32_t monitorHeight;
    BOOL displayDebugInfo;
    uint32_t minimumTimerResolution;
    uint32_t maximumTimerResolution;
    uint32_t currentTimerResolution;
    DWORD handleCount;
    PROCESS_MEMORY_COUNTERS_EX memInfo;
    SYSTEM_INFO systemInfo;
    int64_t currentSystemTime;
    int64_t previousSystemTime;
    double cpuPercent;
} GAMEPERFORMANCEDATA;

typedef struct PLAYER {
    char name[12];
    int32_t screenPosX;
    int32_t screenPosY;
    int32_t hp;
    int32_t strength;
    int32_t mp;
} PLAYER;

LRESULT CALLBACK MainWindowProc(_In_ HWND windowHandle, _In_ UINT message, _In_ WPARAM wParam, _In_ LPARAM lParam);
DWORD CreateMainGameWindow(void);
BOOL GameIsAlreadyRunning(void);
void ProcessPlayerInput(void);
void RenderFrameGraphics(void);

#ifdef SIMD
void ClearScreen(_In_ __m128i* color);
#else
void ClearScreen(_In_ PIXEL32* pixel);
#endif