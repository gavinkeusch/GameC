#pragma once

#ifdef _DEBUG
    #define ASSERT(expression) if (!(expression)) { *(int*)0 = 0; };
#else
    #define ASSERT(expression) ((void)0);
#endif

#define GAME_NAME       "Adventures In C"
#define GAME_RES_WIDTH  384
#define GAME_RES_HEIGHT 240
#define GAME_BPP        32
#define GAME_DRAWING_AREA_MEMORY_SIZE (GAME_RES_WIDTH * GAME_RES_HEIGHT * (GAME_BPP / 8))
#define CALCULATE_AVG_FPS_EVERY_X_FRAMES 120
#define TARGET_MICROSECONDS_PER_FRAME 16667ULL
#define SIMD

#define SUIT_0 0
#define SUIT_1 1
#define SUIT_2 2

#define FACING_DOWN_0  0
#define FACING_DOWN_1  1
#define FACING_DOWN_2  2
#define FACING_LEFT_0  3
#define FACING_LEFT_1  4
#define FACING_LEFT_2  5
#define FACING_RIGHT_0 6
#define FACING_RIGHT_1 7
#define FACING_RIGHT_2 8
#define FACING_UP_0    9
#define FACING_UP_1    10
#define FACING_UP_2    11

typedef enum DIRECTION {
    DOWN = 0,
    LEFT = 3,
    RIGHT = 6,
    UP = 9
} DIRECTION;

#define FONT_SHEET_CHARACTERS_PER_ROW 98

typedef enum LOGLEVEL {
    LOG_NONE,
    LOG_ERROR,
    LOG_WARN,
    LOG_INFO,
    LOG_DEBUG
} LOGLEVEL;

typedef enum GAMESTATE {
    OPENINGSPLASHSCREEN,
    TITLESCREEN,
    OVERWORLD,
    BATTLE,
    OPTIONSCREEN,
    EXITYESNOSCREEN
} GAMESTATE;

#define LOG_FILE_NAME GAME_NAME ".log"

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

typedef struct HERO {
    char name[12];
    GAMEBITMAP sprite[3][12];
    int16_t screenPosX;
    int16_t screenPosY;
    uint8_t movementRemaining;
    DIRECTION direction;
    uint8_t currentArmor;
    uint8_t spriteIndex;
    int32_t hp;
    int32_t strength;
    int32_t mp;
} HERO;

typedef struct REGISTRYPARAMS {
    LOGLEVEL logLevel;
} REGISTRYPARAMS;

LRESULT CALLBACK MainWindowProc(_In_ HWND windowHandle, _In_ UINT message, _In_ WPARAM wParam, _In_ LPARAM lParam);

DWORD CreateMainGameWindow(void);
BOOL GameIsAlreadyRunning(void);

void ProcessPlayerInput(void);

DWORD Load32BppBitmapFromFile(_In_ char* fileName, _Inout_ GAMEBITMAP* gameBitmap);

DWORD InitializeHero(void);

void Blit32BppBitmapToBuffer(_In_ GAMEBITMAP* gameBitmap, _In_ uint16_t x, _In_ uint16_t y);
void BlitStringToBuffer(_In_ char* string, _In_ GAMEBITMAP* fontSheet, _In_ PIXEL32* color, _In_ uint16_t x, _In_ uint16_t y);

void RenderFrameGraphics(void);

DWORD LoadRegistryParameters(void);
void LogMessageA(_In_ LOGLEVEL logLevel, _In_ char* message, _In_ ...);

void DrawDebugInfo(void);

void FindFirstConnectedGamepad(void);

#ifdef SIMD
void ClearScreen(_In_ __m128i* color);
#else
void ClearScreen(_In_ PIXEL32* pixel);
#endif

void DrawOpeningSplashScreen(void);
void DrawTitleScreen(void);

void PlayerInputOpeningSplashScreen(void);
void PlayerInputTitleScreen(void);
void PlayerInputOverworld(void);