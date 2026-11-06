/* The install itself: copies the user's CDs, unpacks the official 1.03
 * update, lays the game out the way the original DOS installer did, and puts
 * the PC executable in place (prebuilt, or converted on this PC). */
#include "setup.h"
#include <stdlib.h>
#include <string.h>
#include <shlobj.h>
#include <objbase.h>
#include <shobjidl.h>
#include "setup.h"
#include "install.h"

#define GAME_EXE_SIZE 916367u
#define GAME_EXE_CRC  0xb83e34f1u      /* ALIENSP2.EXE from the 1.03US update */

static const char *sprite_dirs[] = {"ALIENS", "ARME", "DIVERS", "EGGS", "FACEHUG", "FIRST", "HERICK", "QEEN", "SOLDAT"};

static const char default_cfg[] =
    "[DIGITAL]\r\nDeviceName  = Sound Blaster Pro\r\nDeviceIRQ   = 5 *\r\nDeviceDMA   = 1 *\r\nDevicePort  = 220 *\r\n"
    "DeviceID    = 0xe001\r\n\r\n[MIDI]\r\nDeviceName  = Sound Blaster Pro\r\nDevicePort  = 0x388\r\nDeviceID    = 0xa002\r\n\r\n"
    "[HARD DISK]\r\nPre-load    = No\r\n\r\n[CDROM]\r\nLecteur     = D\r\n";

static int fail(const char *msg) { setup_log("ERROR: %s", msg); return 0; }

/* ---------- game icon: the CD's ALIENS.ICO, rebuilt in several sizes ---------- */
static int decode_ico(const u8 *d, size_t n, u32 *rgba /* 32*32 */)
{
    if (n < 22 || d[2] != 1) return 0;
    u32 off = d[18] | d[19] << 8 | d[20] << 16 | (u32)d[21] << 24;
    if (off + 40 > n) return 0;
    const u8 *bi = d + off;
    int w = bi[4], bpp = bi[14];
    if (w != 32 || bpp != 4) return 0;
    const u8 *pal = bi + 40, *xr = pal + 16 * 4, *and = xr + 32 * 16;
    if ((size_t)(and + 32 * 4 - d) > n) return 0;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int row = 31 - y;
            int idx = (xr[row * 16 + x / 2] >> ((x & 1) ? 0 : 4)) & 15;
            int transparent = (and[row * 4 + x / 8] >> (7 - (x & 7))) & 1;
            const u8 *c = pal + idx * 4;
            rgba[y * 32 + x] = transparent ? 0 : (0xffu << 24) | (c[2] << 16) | (c[1] << 8) | c[0];
        }
    return 1;
}

static u8 *icon_image(const u32 *sq, int side, int size, u32 *len)
{
    u32 mask_row = ((size + 31) / 32) * 4;
    *len = 40 + size * size * 4 + mask_row * size;
    u8 *b = calloc(1, *len);
    u32 *px = (u32 *)(b + 40);
    b[0] = 40;
    *(int *)(b + 4) = size;
    *(int *)(b + 8) = size * 2;
    b[12] = 1;
    b[14] = 32;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
            px[(size - 1 - y) * size + x] = sq[(y * side / size) * side + x * side / size];
    return b;
}

int inject_icon(const char *exe, const u8 *ico, size_t n)
{
    u32 src[32 * 32];
    if (!decode_ico(ico, n, src)) return fail("game icon not understood");
    /* crop to the logo (the lower part of the original icon is transparent) */
    int x0 = 32, y0 = 32, x1 = -1, y1 = -1;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            if (src[y * 32 + x] >> 24) { if (x < x0) x0 = x; if (y < y0) y0 = y; if (x > x1) x1 = x; if (y > y1) y1 = y; }
    if (x1 < 0) { x0 = y0 = 0; x1 = y1 = 31; }
    int w = x1 - x0 + 1, h = y1 - y0 + 1, side = w > h ? w : h;
    u32 *sq = calloc(side * side, 4);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            sq[(y + (side - h) / 2) * side + x + (side - w) / 2] = src[(y0 + y) * 32 + x0 + x];
    static const int sizes[] = {16, 24, 32, 48, 64, 128, 256};
    const int ns = 7;
    HANDLE u = BeginUpdateResourceA(exe, FALSE);
    if (!u) { free(sq); return fail("cannot update the game executable's icon"); }
    u8 grp[6 + 7 * 14];
    memset(grp, 0, sizeof grp);
    grp[2] = 1;
    grp[4] = (u8)ns;
    for (int i = 0; i < ns; i++) {
        u32 len;
        u8 *img = icon_image(sq, side, sizes[i], &len);
        UpdateResourceA(u, (LPCSTR)RT_ICON, MAKEINTRESOURCEA(1 + i), MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), img, len);
        free(img);
        u8 *e = grp + 6 + i * 14;
        e[0] = sizes[i] == 256 ? 0 : (u8)sizes[i];
        e[1] = e[0];
        e[4] = 1;
        e[6] = 32;
        memcpy(e + 8, &len, 4);
        u16 id = (u16)(1 + i);
        memcpy(e + 12, &id, 2);
    }
    UpdateResourceA(u, (LPCSTR)RT_GROUP_ICON, MAKEINTRESOURCEA(1), MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), grp, sizeof grp);
    free(sq);
    if (!EndUpdateResourceA(u, FALSE)) return fail("cannot write the game executable's icon");
    return 1;
}

/* ---------- shortcuts / uninstall entry ---------- */
static int make_link(const char *lnk, const char *target, const char *dir, const char *args, const char *desc)
{
    IShellLinkA *sl = NULL;
    IPersistFile *pf = NULL;
    if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkA, (void **)&sl))) return 0;
    sl->lpVtbl->SetPath(sl, target);
    sl->lpVtbl->SetWorkingDirectory(sl, dir);
    if (args) sl->lpVtbl->SetArguments(sl, args);
    sl->lpVtbl->SetDescription(sl, desc);
    sl->lpVtbl->SetIconLocation(sl, target, 0);
    int ok = 0;
    if (SUCCEEDED(sl->lpVtbl->QueryInterface(sl, &IID_IPersistFile, (void **)&pf))) {
        WCHAR w[MAX_PATH * 2];
        MultiByteToWideChar(CP_ACP, 0, lnk, -1, w, MAX_PATH * 2);
        ok = SUCCEEDED(pf->lpVtbl->Save(pf, w, TRUE));
        pf->lpVtbl->Release(pf);
    }
    sl->lpVtbl->Release(sl);
    return ok;
}

void shortcut_paths(char *startmenu, char *desktop)
{
    char p[MAX_PATH];
    SHGetFolderPathA(NULL, CSIDL_PROGRAMS, NULL, 0, p);
    snprintf(startmenu, MAX_PATH * 2, "%s\\%s.lnk", p, GAME_TITLE);
    SHGetFolderPathA(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, p);
    snprintf(desktop, MAX_PATH * 2, "%s\\%s.lnk", p, GAME_TITLE);
}

static void register_uninstall(const char *dir, u64 bytes)
{
    HKEY k;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, UNINSTALL_KEY, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL)) return;
    char s[MAX_PATH * 3];
    RegSetValueExA(k, "DisplayName", 0, REG_SZ, (const BYTE *)GAME_TITLE " (PC Native)", (DWORD)strlen(GAME_TITLE " (PC Native)") + 1);
    snprintf(s, sizeof s, "\"%s\\uninstall.exe\" /uninstall", dir);
    RegSetValueExA(k, "UninstallString", 0, REG_SZ, (const BYTE *)s, (DWORD)strlen(s) + 1);
    snprintf(s, sizeof s, "%s\\aliens.exe,0", dir);
    RegSetValueExA(k, "DisplayIcon", 0, REG_SZ, (const BYTE *)s, (DWORD)strlen(s) + 1);
    RegSetValueExA(k, "InstallLocation", 0, REG_SZ, (const BYTE *)dir, (DWORD)strlen(dir) + 1);
    const char *pub = "PC Native port (fan project)";
    RegSetValueExA(k, "Publisher", 0, REG_SZ, (const BYTE *)pub, (DWORD)strlen(pub) + 1);
    DWORD kb = (DWORD)(bytes / 1024), one = 1;
    RegSetValueExA(k, "EstimatedSize", 0, REG_DWORD, (const BYTE *)&kb, 4);
    RegSetValueExA(k, "NoModify", 0, REG_DWORD, (const BYTE *)&one, 4);
    RegSetValueExA(k, "NoRepair", 0, REG_DWORD, (const BYTE *)&one, 4);
    RegCloseKey(k);
}

/* ---------- convert-on-this-PC mode ---------- */
static int run_wait(const char *cmdline, const char *path_add, const char *logname)
{
    char cmd[8192];
    snprintf(cmd, sizeof cmd, "%s", cmdline);
    STARTUPINFOA si = {sizeof si};
    PROCESS_INFORMATION pi;
    char oldpath[16384] = "";
    GetEnvironmentVariableA("PATH", oldpath, sizeof oldpath);
    if (path_add) {
        char np[17000];
        snprintf(np, sizeof np, "%s;%s", path_add, oldpath);
        SetEnvironmentVariableA("PATH", np);
    }
    HANDLE lf = INVALID_HANDLE_VALUE;
    if (logname) {
        SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
        lf = CreateFileA(logname, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS, 0, NULL);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = si.hStdError = lf;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }
    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, NULL, NULL, &si, &pi);
    if (path_add) SetEnvironmentVariableA("PATH", oldpath);
    if (!ok) { if (lf != INVALID_HANDLE_VALUE) CloseHandle(lf); return -1; }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (lf != INVALID_HANDLE_VALUE) CloseHandle(lf);
    return (int)code;
}

typedef struct { char cmd[4096]; HANDLE proc; } Job;

static int build_game(const char *kit, const char *dir, const char *game_exe, Progress *pr)
{
    char gen[MAX_PATH * 2], obj[MAX_PATH * 2], log[MAX_PATH * 2], bin[MAX_PATH * 2], cmd[8192];
    snprintf(gen, sizeof gen, "%s\\build\\gen", dir);
    snprintf(obj, sizeof obj, "%s\\build\\obj", dir);
    snprintf(log, sizeof log, "%s\\build\\build.log", dir);
    snprintf(bin, sizeof bin, "%s\\mingw64\\bin", kit);
    make_dirs(gen);
    make_dirs(obj);
    setup_log("Converting the game code on this PC (this takes a few minutes)...");
    snprintf(cmd, sizeof cmd, "\"%s\\python\\python.exe\" -I \"%s\\tools\\recomp.py\" \"%s\" \"%s\\re\\functions.tsv\" \"%s\\tools\\native.txt\" \"%s\"",
             kit, kit, game_exe, kit, kit, gen);
    if (run_wait(cmd, NULL, log) != 0) return fail("the recompiler failed (see build\\build.log)");
    progress_add(pr, 50ull << 20);
    /* compile: generated code + runtime, about half the CPU threads at a time */
    char srcs[512][MAX_PATH * 2];
    int nsrc = 0;
    const char *dirs[2] = {gen, NULL};
    char rt[MAX_PATH * 2];
    snprintf(rt, sizeof rt, "%s\\src\\rt", kit);
    dirs[1] = rt;
    for (int d = 0; d < 2; d++) {
        char pat[MAX_PATH * 2];
        snprintf(pat, sizeof pat, "%s\\*.c", dirs[d]);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do if (nsrc < 512) snprintf(srcs[nsrc++], MAX_PATH * 2, "%s\\%s", dirs[d], fd.cFileName);
        while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int par = (int)si.dwNumberOfProcessors / 2;
    if (par < 1) par = 1;
    if (par > 16) par = 16;
    char oldpath[16384] = "", np[17000];
    GetEnvironmentVariableA("PATH", oldpath, sizeof oldpath);
    snprintf(np, sizeof np, "%s;%s", bin, oldpath);
    SetEnvironmentVariableA("PATH", np);
    Job *jobs = calloc(par, sizeof *jobs);
    int next = 0, running = 0, failed = 0;
    u64 per = (200ull << 20) / (nsrc ? nsrc : 1);
    while ((next < nsrc || running) && !failed) {
        for (int j = 0; j < par && next < nsrc; j++) {
            if (jobs[j].proc) continue;
            const char *s = srcs[next++];
            const char *base = strrchr(s, '\\') + 1;
            int is_rt = strstr(s, "\\src\\rt\\") != NULL;
            snprintf(jobs[j].cmd, sizeof jobs[j].cmd,
                     "\"%s\\gcc.exe\" %s -std=gnu11 -O2 -fwrapv -fno-strict-aliasing -w -I\"%s\" -I\"%s\" -I\"%s\\mingw64\\include\\SDL2\" -c \"%s\" -o \"%s\\%s%s.o\"",
                     bin, is_rt ? "-DSDL_MAIN_HANDLED" : "-g0", rt, gen, kit, s, obj, is_rt ? "rt_" : "", base);
            STARTUPINFOA st = {sizeof st};
            PROCESS_INFORMATION pi;
            if (!CreateProcessA(NULL, jobs[j].cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, NULL, NULL, &st, &pi)) {
                failed = 1;
                break;
            }
            CloseHandle(pi.hThread);
            jobs[j].proc = pi.hProcess;
            running++;
        }
        for (int j = 0; j < par; j++) {
            if (!jobs[j].proc) continue;
            if (WaitForSingleObject(jobs[j].proc, 50) == WAIT_OBJECT_0) {
                DWORD code = 1;
                GetExitCodeProcess(jobs[j].proc, &code);
                CloseHandle(jobs[j].proc);
                jobs[j].proc = NULL;
                running--;
                progress_add(pr, per);
                if (code) { setup_log("compile failed: %s", jobs[j].cmd); failed = 1; }
            }
        }
    }
    for (int j = 0; j < par; j++) if (jobs[j].proc) { WaitForSingleObject(jobs[j].proc, INFINITE); CloseHandle(jobs[j].proc); }
    free(jobs);
    SetEnvironmentVariableA("PATH", oldpath);
    if (failed) return fail("compiling the converted game failed");
    snprintf(cmd, sizeof cmd,
             "\"%s\\gcc.exe\" -mwindows -o \"%s\\aliens.exe\" \"%s\\*.o\" -static -lmingw32 \"%s\\mingw64\\lib\\libSDL2.a\" -lm -lkernel32 -luser32 -lgdi32 "
             "-lwinmm -limm32 -lole32 -loleaut32 -lversion -luuid -ladvapi32 -lsetupapi -lshell32 -ldinput8",
             bin, dir, obj, kit);
    /* gcc does not expand wildcards itself on Windows: list the objects */
    {
        char *c = cmd + strlen(cmd);
        (void)c;
        char lst[MAX_PATH * 2];
        snprintf(lst, sizeof lst, "%s\\build\\objs.rsp", dir);
        FILE *f = fopen(lst, "w");
        char pat[MAX_PATH * 2];
        snprintf(pat, sizeof pat, "%s\\*.o", obj);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pat, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                char o[MAX_PATH * 2];
                snprintf(o, sizeof o, "%s\\%s", obj, fd.cFileName);
                for (char *q = o; *q; q++) if (*q == '\\') *q = '/';
                fprintf(f, "\"%s\"\n", o);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
        fclose(f);
        snprintf(cmd, sizeof cmd,
                 "\"%s\\gcc.exe\" -mwindows -o \"%s\\aliens.exe\" @\"%s\" -static -lmingw32 \"%s\\mingw64\\lib\\libSDL2.a\" -lm -lkernel32 -luser32 -lgdi32 "
                 "-lwinmm -limm32 -lole32 -loleaut32 -lversion -luuid -ladvapi32 -lsetupapi -lshell32 -ldinput8",
                 bin, dir, lst, kit);
    }
    if (run_wait(cmd, bin, log) != 0) return fail("linking the converted game failed (see build\\build.log)");
    progress_add(pr, 20ull << 20);
    snprintf(cmd, sizeof cmd, "%s\\build", dir);
    delete_tree(cmd);
    return 1;
}

/* ---------- prebuilt mode ---------- */
static int extract_prebuilt(const char *dst)
{
    HRSRC r = FindResourceA(NULL, "GAMEEXE", (LPCSTR)RT_RCDATA);
    if (!r) return fail("this setup does not contain the prebuilt game - choose \"Convert on this PC\"");
    HGLOBAL g = LoadResource(NULL, r);
    DWORD n = SizeofResource(NULL, r);
    return write_file(dst, LockResource(g), n);
}

int prebuilt_available(void) { return FindResourceA(NULL, "GAMEEXE", (LPCSTR)RT_RCDATA) != NULL; }

int kit_available(char *kit, size_t n)
{
    char me[MAX_PATH * 2];
    GetModuleFileNameA(NULL, me, sizeof me);
    char *s = strrchr(me, '\\');
    if (s) *s = 0;
    snprintf(kit, n, "%s\\builder", me);
    char t[MAX_PATH * 3];
    snprintf(t, sizeof t, "%s\\tools\\recomp.py", kit);
    return path_exists(t);
}

/* ---------- the whole install ---------- */
int do_install(const InstallOpts *o, void (*cb)(u64, u64, void *), void *user)
{
    char p[MAX_PATH * 2], q[MAX_PATH * 2], game[MAX_PATH * 2], hd[MAX_PATH * 2], kit[MAX_PATH * 2];
    setup_log("Install to %s (%s)", o->dir, o->mode == MODE_BUILD ? "convert on this PC" : "prebuilt");
    if (o->mode == MODE_PREBUILT && !prebuilt_available()) return fail("this setup does not contain the prebuilt game");
    if (o->mode == MODE_BUILD && !kit_available(kit, sizeof kit)) return fail("the build kit (builder folder) is missing next to setup");

    CdSrc *c1 = cd_open(o->cd1), *c2 = cd_open(o->cd2);
    if (!c1) return fail("CD 1: not a readable disc, image or folder");
    if (!c2) { cd_close(c1); return fail("CD 2: not a readable disc, image or folder"); }
    if (cd_exists(c1, "ALIENS\\DISK2\\DISK2") && cd_exists(c2, "ALIENS\\DISK1\\DISK1")) { CdSrc *t = c1; c1 = c2; c2 = t; }
    if (!cd_exists(c1, "ALIENS\\DISK1\\DISK1")) { cd_close(c1); cd_close(c2); return fail("CD 1 is not Aliens disc 1"); }
    if (!cd_exists(c2, "ALIENS\\DISK2\\DISK2")) { cd_close(c1); cd_close(c2); return fail("CD 2 is not Aliens disc 2"); }
    size_t pn;
    u8 *patch = read_file(o->patch, &pn);
    if (!patch) { cd_close(c1); cd_close(c2); return fail("cannot read the 1.03 update file"); }

    Progress pr = {0};
    pr.cb = cb;
    pr.user = user;
    pr.total = cd_total_bytes(c1) + cd_total_bytes(c2) + (12ull << 20) + (o->mode == MODE_BUILD ? (270ull << 20) : 0);
    u64 bytes = pr.total;

    snprintf(game, sizeof game, "%s\\game", o->dir);
    snprintf(hd, sizeof hd, "%s\\hd\\ALIENS", game);
    if (!make_dirs(hd)) { free(patch); cd_close(c1); cd_close(c2); return fail("cannot create the install folder"); }

    /* 1. the discs (the game reads them from here: both are always "in the drive") */
    setup_log("Copying %s ...", cd_describe(c1));
    snprintf(p, sizeof p, "%s\\cd1", game);
    if (!cd_copy_all(c1, p, &pr)) { free(patch); cd_close(c1); cd_close(c2); return fail("copying CD 1 failed"); }
    setup_log("Copying %s ...", cd_describe(c2));
    snprintf(p, sizeof p, "%s\\cd2", game);
    if (!cd_copy_all(c2, p, &pr)) { free(patch); cd_close(c1); cd_close(c2); return fail("copying CD 2 failed"); }
    cd_close(c1);
    cd_close(c2);

    /* 2. the 1.03 update into C:\ALIENS */
    setup_log("Unpacking the 1.03 update ...");
    int nf = lha_extract(patch, pn, hd, &pr);
    free(patch);
    if (nf <= 0) return fail("the 1.03 update file could not be unpacked");
    snprintf(p, sizeof p, "%s\\ALIENSP2.EXE", hd);
    size_t en;
    u8 *ex = read_file(p, &en);
    if (!ex || en != GAME_EXE_SIZE || crc32_buf(ex, en) != GAME_EXE_CRC) {
        free(ex);
        return fail("this is not the official 1.03US update (ALIENSP2.EXE does not match)");
    }
    free(ex);

    /* 3. what the DOS installer copied to the hard disk */
    setup_log("Setting up the game files ...");
    snprintf(p, sizeof p, "%s\\GRAPH", hd);
    make_dirs(p);
    {
        char src[MAX_PATH * 2], pat[MAX_PATH * 2];
        snprintf(src, sizeof src, "%s\\cd1\\ALIENS\\INSTALL", game);
        snprintf(pat, sizeof pat, "%s\\*", src);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pat, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                char s[MAX_PATH * 3], d[MAX_PATH * 3];
                snprintf(s, sizeof s, "%s\\%s", src, fd.cFileName);
                snprintf(d, sizeof d, "%s\\%s", p, fd.cFileName);
                copy_file(s, d, NULL);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
        snprintf(q, sizeof q, "%s\\INSTALL.CR", src);
        size_t cn;
        u8 *cr = read_file(q, &cn);
        if (!cr || lha_extract(cr, cn, p, NULL) <= 0) { free(cr); return fail("INSTALL.CR on CD 1 could not be unpacked"); }
        free(cr);
    }
    for (size_t i = 0; i < sizeof sprite_dirs / sizeof *sprite_dirs; i++) {
        char src[MAX_PATH * 2], pat[MAX_PATH * 2], dst[MAX_PATH * 2];
        snprintf(src, sizeof src, "%s\\cd1\\ALIENS\\DISK1\\COMBAT\\SPRITES\\%s", game, sprite_dirs[i]);
        snprintf(dst, sizeof dst, "%s\\%s", hd, sprite_dirs[i]);
        make_dirs(dst);
        snprintf(pat, sizeof pat, "%s\\*", src);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            char s[MAX_PATH * 3], d[MAX_PATH * 3];
            snprintf(s, sizeof s, "%s\\%s", src, fd.cFileName);
            snprintf(d, sizeof d, "%s\\%s", dst, fd.cFileName);
            copy_file(s, d, NULL);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    snprintf(p, sizeof p, "%s\\cd1\\ALIENS\\DISK1\\MUSIC\\MUSIK1.WAV", game);
    snprintf(q, sizeof q, "%s\\MUSIC\\MUSIK1.WAV", hd);
    copy_file(p, q, NULL);
    snprintf(p, sizeof p, "%s\\MIDI", hd);
    make_dirs(p);
    snprintf(p, sizeof p, "%s\\OBJ", hd);
    make_dirs(p);
    snprintf(p, sizeof p, "%s\\hd\\ALIENS.CFG", game);
    if (!path_exists(p)) write_file(p, default_cfg, sizeof default_cfg - 1);
    progress_add(&pr, 10ull << 20);

    /* 4. the game executable */
    char exe[MAX_PATH * 2];
    snprintf(exe, sizeof exe, "%s\\aliens.exe", o->dir);
    snprintf(p, sizeof p, "%s\\ALIENSP2.EXE", hd);
    if (o->mode == MODE_BUILD) { if (!build_game(kit, o->dir, p, &pr)) return 0; }
    else if (!extract_prebuilt(exe)) return 0;

    /* 5. its icon, from the game's own ALIENS.ICO on CD 1 */
    snprintf(p, sizeof p, "%s\\cd1\\ALIENS\\ALIENS.ICO", game);
    size_t in;
    u8 *ico = read_file(p, &in);
    if (ico) { inject_icon(exe, ico, in); free(ico); }

    /* 6. uninstaller, shortcuts */
    char me[MAX_PATH * 2];
    GetModuleFileNameA(NULL, me, sizeof me);
    snprintf(p, sizeof p, "%s\\uninstall.exe", o->dir);
    if (_stricmp(me, p)) CopyFileA(me, p, FALSE);
    register_uninstall(o->dir, bytes);
    char sm[MAX_PATH * 2], dt[MAX_PATH * 2];
    shortcut_paths(sm, dt);
    CoInitialize(NULL);
    if (o->startmenu) make_link(sm, exe, o->dir, NULL, GAME_TITLE);
    if (o->desktop) make_link(dt, exe, o->dir, NULL, GAME_TITLE);
    CoUninitialize();
    progress_add(&pr, 2ull << 20);
    setup_log("Done.");
    return 1;
}

int do_uninstall(const char *dir)
{
    char sm[MAX_PATH * 2], dt[MAX_PATH * 2], p[MAX_PATH * 2];
    shortcut_paths(sm, dt);
    DeleteFileA(sm);
    DeleteFileA(dt);
    RegDeleteKeyA(HKEY_CURRENT_USER, UNINSTALL_KEY);
    snprintf(p, sizeof p, "%s\\game", dir);
    delete_tree(p);
    static const char *files[] = {"aliens.exe", "uninstall.exe", "aliens_pc.ini", "aliens.log", "setup.log"};
    for (size_t i = 0; i < sizeof files / sizeof *files; i++) {
        snprintf(p, sizeof p, "%s\\%s", dir, files[i]);
        DeleteFileA(p);
    }
    snprintf(p, sizeof p, "%s\\build", dir);
    delete_tree(p);
    RemoveDirectoryA(dir);
    return 1;
}
