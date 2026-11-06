/* Aliens: A Comic Book Adventure - PC Native setup.
 *
 * Usage: just run it.  Command line (testing / scripted installs):
 *   setup.exe /silent /cd1 <iso|drive|folder> /cd2 <...> /patch <ALIEN103.EXE>
 *             /dir <install folder> [/build] [/nodesktop]
 *   uninstall.exe /uninstall
 */
#include "install.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include "install.h"

static HWND hwnd, e_cd1, e_cd2, e_patch, e_dir, r_pre, r_build, c_desk, c_start, b_install, b_close, prog, st, lst;
static HFONT font;
static FILE *logf;
static int busy, silent, done_ok, quiet;
static InstallOpts opts;
#define WM_SETUP_LOG (WM_APP + 1)
#define WM_SETUP_PROG (WM_APP + 2)
#define WM_SETUP_DONE (WM_APP + 3)

void setup_log(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (logf) { fprintf(logf, "%s\n", buf); fflush(logf); }
    if (hwnd && !silent) PostMessageA(hwnd, WM_SETUP_LOG, 0, (LPARAM)_strdup(buf));
}

static void on_progress(u64 done, u64 total, void *user)
{
    (void)user;
    static int last = -1;
    int pc = total ? (int)(done * 1000 / total) : 0;
    if (pc > 1000) pc = 1000;
    if (pc != last && hwnd && !silent) { last = pc; PostMessageA(hwnd, WM_SETUP_PROG, pc, 0); }
}

static DWORD WINAPI worker(LPVOID p)
{
    (void)p;
    int ok = do_install(&opts, on_progress, NULL);
    PostMessageA(hwnd, WM_SETUP_DONE, ok, 0);
    return 0;
}

/* ---------- defaults ---------- */
static int disc_in(const char *root, int which)
{
    char p[MAX_PATH];
    snprintf(p, sizeof p, "%sALIENS\\DISK%d\\DISK%d", root, which, which);
    return path_exists(p);
}

static void find_defaults(void)
{
    /* CD drives with the discs */
    DWORD drives = GetLogicalDrives();
    for (int i = 2; i < 26; i++) {
        if (!(drives & (1u << i))) continue;
        char root[4] = {(char)('A' + i), ':', '\\', 0};
        if (GetDriveTypeA(root) != DRIVE_CDROM) continue;
        if (disc_in(root, 1) && !opts.cd1[0]) snprintf(opts.cd1, sizeof opts.cd1, "%s", root);
        if (disc_in(root, 2) && !opts.cd2[0]) snprintf(opts.cd2, sizeof opts.cd2, "%s", root);
    }
    /* files next to setup: *1*.iso, *2*.iso, ALIEN103.EXE */
    char me[MAX_PATH * 2];
    GetModuleFileNameA(NULL, me, sizeof me);
    char *s = strrchr(me, '\\');
    if (s) *s = 0;
    char pat[MAX_PATH * 2];
    snprintf(pat, sizeof pat, "%s\\*", me);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const char *n = fd.cFileName;
            const char *ext = strrchr(n, '.');
            char full[MAX_PATH * 3];
            snprintf(full, sizeof full, "%s\\%s", me, n);
            if (!_stricmp(n, "ALIEN103.EXE") && !opts.patch[0]) snprintf(opts.patch, sizeof opts.patch, "%s", full);
            if (ext && (!_stricmp(ext, ".iso") || !_stricmp(ext, ".cue"))) {
                CdSrc *c = cd_open(full);
                if (c) {
                    if (cd_exists(c, "ALIENS\\DISK1\\DISK1") && !opts.cd1[0]) snprintf(opts.cd1, sizeof opts.cd1, "%s", full);
                    if (cd_exists(c, "ALIENS\\DISK2\\DISK2") && !opts.cd2[0]) snprintf(opts.cd2, sizeof opts.cd2, "%s", full);
                    cd_close(c);
                }
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    if (!opts.dir[0]) snprintf(opts.dir, sizeof opts.dir, "C:\\Games\\Aliens PC");
    opts.startmenu = 1;
    opts.desktop = 1;
    opts.mode = prebuilt_available() ? MODE_PREBUILT : MODE_BUILD;
}

/* ---------- browse ---------- */
static void browse_file(HWND edit, const char *filter, const char *title)
{
    char f[MAX_PATH * 2] = "";
    GetWindowTextA(edit, f, sizeof f);
    if (is_dir(f)) f[0] = 0;
    OPENFILENAMEA of = {sizeof of};
    of.hwndOwner = hwnd;
    of.lpstrFilter = filter;
    of.lpstrFile = f;
    of.nMaxFile = sizeof f;
    of.lpstrTitle = title;
    of.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameA(&of)) SetWindowTextA(edit, f);
}

static void browse_folder(HWND edit, const char *title)
{
    BROWSEINFOA bi = {0};
    char name[MAX_PATH];
    bi.hwndOwner = hwnd;
    bi.pszDisplayName = name;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST il = SHBrowseForFolderA(&bi);
    if (il) {
        char p[MAX_PATH];
        if (SHGetPathFromIDListA(il, p)) SetWindowTextA(edit, p);
        CoTaskMemFree(il);
    }
}

/* ---------- window ---------- */
enum { ID_CD1F = 101, ID_CD1D, ID_CD2F, ID_CD2D, ID_PATCH, ID_DIR, ID_INSTALL, ID_CLOSE };

static HWND mk(const char *cls, const char *text, DWORD style, int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExA(!strcmp(cls, "EDIT") ? WS_EX_CLIENTEDGE : 0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                             hwnd, (HMENU)(INT_PTR)id, GetModuleHandleA(NULL), NULL);
    SendMessageA(c, WM_SETFONT, (WPARAM)font, TRUE);
    return c;
}

static void build_ui(void)
{
    int y = 14;
    mk("STATIC", "Install Aliens: A Comic Book Adventure as a native Windows game.\n"
                 "You need your two Aliens CDs (discs, ISO images or copied folders) and the official 1.03 update (ALIEN103.EXE).",
       0, 16, y, 600, 36, 0);
    y += 48;
    mk("STATIC", "CD 1:", 0, 16, y + 4, 90, 20, 0);
    e_cd1 = mk("EDIT", opts.cd1, ES_AUTOHSCROLL, 110, y, 360, 24, 0);
    mk("BUTTON", "Image...", 0, 478, y, 66, 24, ID_CD1F);
    mk("BUTTON", "Folder...", 0, 548, y, 66, 24, ID_CD1D);
    y += 32;
    mk("STATIC", "CD 2:", 0, 16, y + 4, 90, 20, 0);
    e_cd2 = mk("EDIT", opts.cd2, ES_AUTOHSCROLL, 110, y, 360, 24, 0);
    mk("BUTTON", "Image...", 0, 478, y, 66, 24, ID_CD2F);
    mk("BUTTON", "Folder...", 0, 548, y, 66, 24, ID_CD2D);
    y += 32;
    mk("STATIC", "1.03 update:", 0, 16, y + 4, 90, 20, 0);
    e_patch = mk("EDIT", opts.patch, ES_AUTOHSCROLL, 110, y, 360, 24, 0);
    mk("BUTTON", "Browse...", 0, 478, y, 136, 24, ID_PATCH);
    y += 32;
    mk("STATIC", "Install to:", 0, 16, y + 4, 90, 20, 0);
    e_dir = mk("EDIT", opts.dir, ES_AUTOHSCROLL, 110, y, 360, 24, 0);
    mk("BUTTON", "Browse...", 0, 478, y, 136, 24, ID_DIR);
    y += 40;
    r_pre = mk("BUTTON", "Install the prebuilt game (fast)", BS_AUTORADIOBUTTON | WS_GROUP, 16, y, 300, 22, 0);
    r_build = mk("BUTTON", "Convert the game on this PC (needs the build kit, a few minutes)", BS_AUTORADIOBUTTON, 16, y + 24, 520, 22, 0);
    char kit[MAX_PATH * 2];
    if (!prebuilt_available()) EnableWindow(r_pre, FALSE);
    if (!kit_available(kit, sizeof kit)) EnableWindow(r_build, FALSE);
    SendMessageA(opts.mode == MODE_BUILD ? r_build : r_pre, BM_SETCHECK, BST_CHECKED, 0);
    y += 56;
    c_start = mk("BUTTON", "Start menu shortcut", BS_AUTOCHECKBOX, 16, y, 200, 22, 0);
    c_desk = mk("BUTTON", "Desktop shortcut", BS_AUTOCHECKBOX, 230, y, 200, 22, 0);
    SendMessageA(c_start, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageA(c_desk, BM_SETCHECK, BST_CHECKED, 0);
    y += 34;
    prog = mk(PROGRESS_CLASSA, "", 0, 16, y, 598, 18, 0);
    SendMessageA(prog, PBM_SETRANGE32, 0, 1000);
    y += 24;
    st = mk("STATIC", "Ready. About 1.5 GB of free space is needed.", 0, 16, y, 598, 20, 0);
    y += 24;
    lst = mk("LISTBOX", "", WS_VSCROLL | WS_BORDER | LBS_NOINTEGRALHEIGHT, 16, y, 598, 110, 0);
    y += 120;
    b_install = mk("BUTTON", "Install", BS_DEFPUSHBUTTON, 418, y, 96, 30, ID_INSTALL);
    b_close = mk("BUTTON", "Cancel", 0, 518, y, 96, 30, ID_CLOSE);
}

static void start_install(void)
{
    GetWindowTextA(e_cd1, opts.cd1, sizeof opts.cd1);
    GetWindowTextA(e_cd2, opts.cd2, sizeof opts.cd2);
    GetWindowTextA(e_patch, opts.patch, sizeof opts.patch);
    GetWindowTextA(e_dir, opts.dir, sizeof opts.dir);
    opts.mode = SendMessageA(r_build, BM_GETCHECK, 0, 0) == BST_CHECKED ? MODE_BUILD : MODE_PREBUILT;
    opts.startmenu = SendMessageA(c_start, BM_GETCHECK, 0, 0) == BST_CHECKED;
    opts.desktop = SendMessageA(c_desk, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (!opts.cd1[0] || !opts.cd2[0] || !opts.patch[0] || !opts.dir[0]) {
        MessageBoxA(hwnd, "Please fill in both CDs, the 1.03 update and the install folder.", "Aliens setup", MB_ICONWARNING);
        return;
    }
    busy = 1;
    EnableWindow(b_install, FALSE);
    SetWindowTextA(b_close, "Cancel");
    SetWindowTextA(st, "Installing...");
    CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case ID_CD1F: browse_file(e_cd1, "CD images (*.iso;*.cue;*.bin)\0*.iso;*.cue;*.bin\0All files\0*.*\0", "Aliens CD 1"); break;
        case ID_CD2F: browse_file(e_cd2, "CD images (*.iso;*.cue;*.bin)\0*.iso;*.cue;*.bin\0All files\0*.*\0", "Aliens CD 2"); break;
        case ID_CD1D: browse_folder(e_cd1, "Aliens CD 1 (drive or folder)"); break;
        case ID_CD2D: browse_folder(e_cd2, "Aliens CD 2 (drive or folder)"); break;
        case ID_PATCH: browse_file(e_patch, "1.03 update (ALIEN103.EXE)\0ALIEN103.EXE;*.exe\0All files\0*.*\0", "Aliens 1.03 update"); break;
        case ID_DIR: browse_folder(e_dir, "Install folder"); break;
        case ID_INSTALL:
            if (done_ok) {
                char exe[MAX_PATH * 3];
                snprintf(exe, sizeof exe, "%s\\aliens.exe", opts.dir);
                ShellExecuteA(NULL, "open", exe, NULL, opts.dir, SW_SHOWNORMAL);
                DestroyWindow(h);
            } else start_install();
            break;
        case ID_CLOSE:
            if (busy && MessageBoxA(h, "Stop the installation?", "Aliens setup", MB_YESNO | MB_ICONQUESTION) != IDYES) break;
            if (busy) ExitProcess(1);
            DestroyWindow(h);
            break;
        }
        return 0;
    case WM_SETUP_LOG: {
        char *s = (char *)l;
        int i = (int)SendMessageA(lst, LB_ADDSTRING, 0, (LPARAM)s);
        SendMessageA(lst, LB_SETCURSEL, i, 0);
        SetWindowTextA(st, s);
        free(s);
        return 0;
    }
    case WM_SETUP_PROG:
        SendMessageA(prog, PBM_SETPOS, w, 0);
        return 0;
    case WM_SETUP_DONE:
        busy = 0;
        if (w) {
            done_ok = 1;
            SendMessageA(prog, PBM_SETPOS, 1000, 0);
            SetWindowTextA(st, "Aliens is installed.");
            SetWindowTextA(b_install, "Play");
            EnableWindow(b_install, TRUE);
            SetWindowTextA(b_close, "Close");
        } else {
            SetWindowTextA(st, "The installation did not finish - see the messages above.");
            EnableWindow(b_install, TRUE);
            SetWindowTextA(b_close, "Close");
        }
        return 0;
    case WM_CLOSE:
        if (busy && MessageBoxA(h, "Stop the installation?", "Aliens setup", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
        if (busy) ExitProcess(1);
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

/* ---------- uninstall ---------- */
static int uninstall_main(int phase2, const char *dir)
{
    if (!phase2) {
        char me[MAX_PATH * 2], d[MAX_PATH * 2];
        GetModuleFileNameA(NULL, me, sizeof me);
        snprintf(d, sizeof d, "%s", me);
        char *s = strrchr(d, '\\');
        if (s) *s = 0;
        if (MessageBoxA(NULL, "Remove Aliens: A Comic Book Adventure (PC Native) and its saved games?", "Aliens uninstall",
                        MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
        /* run from a temporary copy so this folder can be removed */
        char tmp[MAX_PATH], copy[MAX_PATH * 2], cmd[MAX_PATH * 5];
        GetTempPathA(sizeof tmp, tmp);
        snprintf(copy, sizeof copy, "%saliens_uninstall.exe", tmp);
        CopyFileA(me, copy, FALSE);
        snprintf(cmd, sizeof cmd, "\"%s\" /uninstall2 \"%s\"", copy, d);
        STARTUPINFOA si = {sizeof si};
        PROCESS_INFORMATION pi;
        if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
        return 0;
    }
    Sleep(800);
    do_uninstall(dir);
    if (!quiet) MessageBoxA(NULL, "Aliens: A Comic Book Adventure has been removed.", "Aliens uninstall", MB_ICONINFORMATION);
    char me[MAX_PATH * 2];
    GetModuleFileNameA(NULL, me, sizeof me);
    MoveFileExA(me, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
    return 0;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    (void)prev; (void)cmdline;
    int argc;
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    char **argv = calloc(argc + 1, sizeof *argv);
    for (int i = 0; i < argc; i++) {
        int n = WideCharToMultiByte(CP_ACP, 0, wargv[i], -1, NULL, 0, NULL, NULL);
        argv[i] = malloc(n);
        WideCharToMultiByte(CP_ACP, 0, wargv[i], -1, argv[i], n, NULL, NULL);
    }
    for (int i = 1; i < argc; i++) if (!_stricmp(argv[i], "/quiet")) quiet = 1;
    for (int i = 1; i < argc; i++) {
        if (!_stricmp(argv[i], "/uninstall")) return uninstall_main(0, NULL);
        if (!_stricmp(argv[i], "/uninstall2") && i + 1 < argc) return uninstall_main(1, argv[i + 1]);
    }
    char logp[MAX_PATH];
    GetTempPathA(sizeof logp, logp);
    strncat(logp, "aliens_setup.log", sizeof logp - strlen(logp) - 1);
    logf = fopen(logp, "w");
    find_defaults();
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : "";
        if (!_stricmp(a, "/silent")) silent = 1;
        else if (!_stricmp(a, "/build")) opts.mode = MODE_BUILD;
        else if (!_stricmp(a, "/prebuilt")) opts.mode = MODE_PREBUILT;
        else if (!_stricmp(a, "/nodesktop")) opts.desktop = 0;
        else if (!_stricmp(a, "/nostartmenu")) opts.startmenu = 0;
        else if (!_stricmp(a, "/cd1")) { snprintf(opts.cd1, sizeof opts.cd1, "%s", v); i++; }
        else if (!_stricmp(a, "/cd2")) { snprintf(opts.cd2, sizeof opts.cd2, "%s", v); i++; }
        else if (!_stricmp(a, "/patch")) { snprintf(opts.patch, sizeof opts.patch, "%s", v); i++; }
        else if (!_stricmp(a, "/dir")) { snprintf(opts.dir, sizeof opts.dir, "%s", v); i++; }
    }
    if (silent) {
        int ok = do_install(&opts, NULL, NULL);
        if (logf) fclose(logf);
        return ok ? 0 : 1;
    }
    INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    CoInitialize(NULL);
    NONCLIENTMETRICSA ncm = {sizeof ncm};
    SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    font = CreateFontIndirectA(&ncm.lfMessageFont);
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "AliensSetup";
    wc.hIcon = LoadIconA(inst, MAKEINTRESOURCEA(1));
    RegisterClassA(&wc);
    RECT r = {0, 0, 630, 520};
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    hwnd = CreateWindowA("AliensSetup", "Aliens: A Comic Book Adventure - PC Native Setup", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                         CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    build_ui();
    ShowWindow(hwnd, show);
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageA(hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    }
    if (logf) fclose(logf);
    return 0;
}
