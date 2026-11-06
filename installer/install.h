#pragma once
#include "setup.h"

#define GAME_TITLE "Aliens - A Comic Book Adventure"
#define UNINSTALL_KEY "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\AliensCBA_PCNative"

enum { MODE_PREBUILT = 0, MODE_BUILD = 1 };

typedef struct {
    char cd1[MAX_PATH * 2], cd2[MAX_PATH * 2], patch[MAX_PATH * 2], dir[MAX_PATH * 2];
    int mode, desktop, startmenu;
} InstallOpts;

int do_install(const InstallOpts *o, void (*cb)(u64 done, u64 total, void *user), void *user);
int do_uninstall(const char *dir);
int prebuilt_available(void);
int kit_available(char *kit, size_t n);
int inject_icon(const char *exe, const u8 *ico, size_t n);
void shortcut_paths(char *startmenu, char *desktop);
