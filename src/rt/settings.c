/* PC-side options: aliens_pc.ini next to aliens.exe (created with defaults
 * on first run).  These only change how the PC presents the game; the
 * defaults keep the original behaviour. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include "rt.h"

int opt_click_once;          /* holding the left/right button = one click */
int opt_click_ms = 100;      /* how long a press is reported as held */

static char ini_path[700];

static const char default_ini[] =
    "; Aliens: A Comic Book Adventure - PC options\r\n"
    "\r\n"
    "[input]\r\n"
    "; The original game treats a held mouse button as repeated clicks.\r\n"
    "; ClickOnce=1 turns each press into a single click (F1 toggles in game).\r\n"
    "ClickOnce=0\r\n"
    "; How long one click is reported to the game, in milliseconds.\r\n"
    "ClickMs=100\r\n";

void settings_load(const char *exe_dir)
{
    snprintf(ini_path, sizeof ini_path, "%s\\aliens_pc.ini", exe_dir);
    if (GetFileAttributesA(ini_path) == INVALID_FILE_ATTRIBUTES) {
        FILE *f = fopen(ini_path, "wb");
        if (f) { fwrite(default_ini, 1, sizeof default_ini - 1, f); fclose(f); }
    }
    opt_click_once = GetPrivateProfileIntA("input", "ClickOnce", 0, ini_path) != 0;
    opt_click_ms = GetPrivateProfileIntA("input", "ClickMs", 100, ini_path);
    if (opt_click_ms < 20) opt_click_ms = 20;
    if (opt_click_ms > 1000) opt_click_ms = 1000;
    rt_log("options: ClickOnce=%d ClickMs=%d\n", opt_click_once, opt_click_ms);
}

void settings_save_click_once(void)
{
    WritePrivateProfileStringA("input", "ClickOnce", opt_click_once ? "1" : "0", ini_path);
}
