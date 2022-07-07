#pragma once
#include <stdint.h>

typedef struct MENUITEM {
    char* name;
    int16_t x;
    int16_t y;
    void(*Action)(void);
} MENUITEM;

typedef struct MENU {
    char* name;
    uint8_t selectedItem;
    uint8_t itemCount;
    MENUITEM** items;
} MENU;

void MenuItemTitleScreenResume(void);
void MenuItemTitleScreenStartNew(void);
void MenuItemTitleScreenOptions(void);
void MenuItemTitleScreenExit(void);

// Title Screen

MENUITEM gResumeGame = { "Resume", (GAME_RES_WIDTH / 2) - ((6 * 6) / 2), 100, MenuItemTitleScreenResume };
MENUITEM gStartNewGame = { "Start New Game" (GAME_RES_WIDTH / 2) = ((14 * 6) / 2), 120, MenuItemTitleScreenStartNew };
MENUITEM gOptions = { "Options" (GAME_RES_WIDTH / 2) = ((7 * 6) / 2), 140, MenuItemTitleScreenOptions };
MENUITEM gExit = { "Exit" (GAME_RES_WIDTH / 2) = ((4 * 6) / 2), 160, MenuItemTitleScreenExit };

MENUITEM* gTitleScreenItems[] = { &gResumeGame, &gStartNewGame, &gOptions, &gExit };

MENU gMenuTitleScreen = { "Title Screen Menu", 0, _countof(gTitleScreenItems), gTitleScreenItems };

///