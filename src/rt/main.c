/* Aliens: A Comic Book Adventure for Windows - entry point and core runtime.
 *
 * The game's own code (recompiled into src/gen) runs on a 4 GB guest address
 * space.  This file sets that up, loads the original ALIENSP2.EXE image from
 * the user's install, and starts the program through its own startup code.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include "rt.h"

CPU cpu;
u32 rt_tail_ret;
u8 *MEM;
u8 rt_parity_tab[256];
FILE *rt_logf;
char rt_root[512];

void rt_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (rt_logf) { vfprintf(rt_logf, fmt, ap); fflush(rt_logf); }
    va_end(ap);
}

void rt_fatal(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    rt_log("FATAL: %s\n", buf);
    rt_log("regs: eax=%08x ecx=%08x edx=%08x ebx=%08x esp=%08x ebp=%08x esi=%08x edi=%08x\n",
           cpu.r[0], cpu.r[1], cpu.r[2], cpu.r[3], cpu.r[4], cpu.r[5], cpu.r[6], cpu.r[7]);
    for (int i = 0; i < 16; i++) rt_log(" [esp+%02x]=%08x", i * 4, RD32(cpu.r[4] + i * 4));
    rt_log("\n");
    if (!getenv("ALIENS_HIDDEN")) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Aliens", buf, NULL);
    exit(1);
}

/* ---- function dispatch ---- */
void (*rt_lookup(u32 addr))(void)
{
    int lo = 0, hi = rt_nfuncs - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (rt_functab[mid].addr == addr) return rt_functab[mid].fn;
        if (rt_functab[mid].addr < addr) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

extern void irq_magic(u32 n);
static void magic_return(u32 target)
{
    /* the game chained to the handler that was installed before it: do what
       the original BIOS handler did, then return like iretd */
    irq_magic(target - MAGIC_IRET);
    cpu.r[R_ESP] += 12;
}

void rt_call(u32 target, u32 at)
{
    rt_poll();
    if (target >= MAGIC_IRET && target < MAGIC_IRET + 0x100) {
        /* far call/jmp to an old handler through a 48-bit pointer is handled
           by the caller sites; a near call into it just returns */
        irq_magic(target - MAGIC_IRET);
        cpu.r[R_ESP] += 4;
        return;
    }
    void (*fn)(void) = rt_lookup(target);
    if (!fn) rt_fatal("call to unknown code %08x from %08x", target, at);
    fn();
}

void rt_jump(u32 target, u32 at)
{
    if (target >= MAGIC_IRET && target < MAGIC_IRET + 0x100) { magic_return(target); return; }
    void (*fn)(void) = rt_lookup(target);
    if (!fn) rt_fatal("jump to unknown code %08x from %08x", target, at);
    fn();
}

void rt_call_guest(u32 addr)
{
    void (*fn)(void) = rt_lookup(addr);
    if (!fn) rt_fatal("rt_call_guest: no function at %08x", addr);
    cpu.r[R_ESP] -= 4;
    WR32(cpu.r[R_ESP], MAGIC_IRET + 0xff);
    fn();
}

/* heuristic backtrace: return addresses into the code object found on the guest stack */
void rt_backtrace(const char *tag)
{
    u32 sp = cpu.r[R_ESP];
    int n = 0;
    rt_log("  %s backtrace:", tag);
    for (u32 i = 0; i < 256 && n < 12; i++) {
        u32 v = RD32(sp + i * 4);
        if (v >= 0x410000 && v < 0x482000 && RD8(v - 5) == 0xe8) { rt_log(" %08x", v - 5); n++; }
    }
    rt_log("\n");
}

static int unimpl_count;
void rt_unimpl(u32 at, const char *what)
{
    if (unimpl_count++ < 200) rt_log("unimplemented instruction at %08x: %s\n", at, what);
}
void rt_bad_code(u32 at) { rt_fatal("execution reached undecodable code at %08x", at); }
void rt_fell_off(u32 at) { rt_fatal("fell off the end of a block at %08x", at); }
void rt_noreturn(u32 at) { rt_fatal("noreturn call returned at %08x", at); }
void rt_divide_error(u32 at) { rt_fatal("divide error at %08x", at); }

/* ---- x87 helpers ---- */
long double rt_fround(long double v)
{
    switch ((cpu.fcw >> 10) & 3) {
    case 0: return nearbyintl(v);
    case 1: return floorl(v);
    case 2: return ceill(v);
    default: return truncl(v);
    }
}

s64 rt_fist(long double v, int bits)
{
    long double r = rt_fround(v);
    long double lim = bits == 16 ? 32768.0L : bits == 32 ? 2147483648.0L : 9223372036854775808.0L;
    if (!(r >= -lim && r < lim)) return bits == 16 ? (s64)(s16)0x8000 : bits == 32 ? (s64)(s32)0x80000000 : INT64_MIN;
    return (s64)r;
}

void rt_fpu_save(u32 a)
{
    memset(MEM + a, 0, 108);
    WR16(a, cpu.fcw);
    WR16(a + 4, FPU_SW());
    for (int i = 0; i < 8; i++) WRF80(a + 28 + i * 10, ST(i));
    FPU_INIT();
}

void rt_fpu_restore(u32 a)
{
    cpu.fcw = RD16(a);
    u16 sw = RD16(a + 4);
    cpu.fsw = sw & ~0x3800;
    cpu.ftop = (sw >> 11) & 7;
    for (int i = 0; i < 8; i++) ST(i) = RDF80(a + 28 + i * 10);
}

/* ---- guest memory ---- */
static LONG WINAPI guest_fault(EXCEPTION_POINTERS *ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        ULONG_PTR addr = ep->ExceptionRecord->ExceptionInformation[1];
        if (addr >= (ULONG_PTR)MEM && addr < (ULONG_PTR)MEM + GUEST_RESERVE) {
            u32 ga = (u32)(addr - (ULONG_PTR)MEM);
            /* commit on demand: guest code may touch memory it never asked for */
            if (VirtualAlloc((void *)(addr & ~(ULONG_PTR)0xffff), 0x10000, MEM_COMMIT, PAGE_READWRITE)) {
                static int warned;
                if (warned++ < 20) rt_log("note: committed guest page at %08x on demand\n", ga);
                return EXCEPTION_CONTINUE_EXECUTION;
            }
            rt_log("guest access fault at %08x\n", ga);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void mem_init(void)
{
    MEM = VirtualAlloc(NULL, GUEST_RESERVE, MEM_RESERVE, PAGE_NOACCESS);
    if (!MEM) rt_fatal("cannot reserve guest address space");
    if (!VirtualAlloc(MEM, GUEST_COMMIT, MEM_COMMIT, PAGE_READWRITE)) rt_fatal("cannot commit guest memory");
    AddVectoredExceptionHandler(1, guest_fault);
    for (int i = 0; i < 256; i++) {
        int b = i, p = 1;
        while (b) { p ^= b & 1; b >>= 1; }
        rt_parity_tab[i] = (u8)p;
    }
}

/* ---- DOS process environment for the program's own startup code ---- */
static void setup_process(const char *args)
{
    /* PSP at 0x1000, environment at 0x1200 */
    u32 psp = 0x1000, env = 0x1200;
    memset(MEM + psp, 0, 0x100);
    WR16(psp, 0x20cd);
    WR16(psp + 0x2c, SEL_ENV);
    size_t n = strlen(args);
    if (n > 126) n = 126;
    WR8(psp + 0x80, (u8)(n ? n + 1 : 0));
    WR8(psp + 0x81, ' ');
    memcpy(MEM + psp + 0x82, args, n);
    WR8(psp + 0x82 + n, 0x0d);
    if (!n) { WR8(psp + 0x80, 0); WR8(psp + 0x81, 0x0d); }
    static const char envs[] = "PATH=C:\\\0COMSPEC=C:\\COMMAND.COM\0BLASTER=A220 I5 D1 T4\0\0\x01\0C:\\ALIENS\\ALIENSP2.EXE\0";
    memcpy(MEM + env, envs, sizeof envs);
    sel_alloc(0, 0);  /* make sure the table exists */
}

/* The Watcom C startup (cstart at 46331c) set up for the PC: the same
   globals it fills in from the DOS process (PSP, environment, extender
   type), then __InitRtns and __CMain -> main(argc, argv). */
#define W_STACKTOP   0x4df64cu
#define W_CURBRK     0x4df650u
#define W_DYNEND     0x4df654u   /* command line copy, parsed into argv */
#define W_PGMNAME    0x4df658u
#define W_PSP        0x4df65cu
#define W_STACKLOW   0x4df660u
#define W_STKTOP0    0x4df664u
#define W_NO87       0x4df67cu
#define W_EXTENDER   0x4df67eu
#define W_EXTSUB     0x4df67fu
#define W_ENVPTR     0x4df681u
#define W_ENVSEL     0x4df685u
#define W_OSMAJOR    0x4df687u
#define W_OSMINOR    0x4df688u
#define W_INITRTNS   0x474ec6u
#define W_CMAIN      0x474e76u

static void native_cstart(u32 stack, const char *args)
{
    u32 sp = stack & ~3u;
    WR32(W_STKTOP0, sp);
    WR32(W_CURBRK, sp);          /* heap grows from the end of the image */
    WR16(W_PSP, SEL_PSP);
    WR8(W_OSMAJOR, 7);
    WR8(W_OSMINOR, 0);
    WR8(W_EXTENDER, 1);          /* DOS/4G-compatible flat model */
    WR8(W_EXTSUB, 0);
    WR32(W_ENVPTR, 0x1200);
    WR16(W_ENVSEL, SEL_ENV);
    /* "<args>\0" then the program name, at the bottom of the stack area */
    u32 a = 0x6324e0u;
    WR32(W_DYNEND, a);
    size_t n = strlen(args);
    memcpy(MEM + a, args, n);
    WR8(a + n, 0);
    u32 pg = a + n + 1;
    static const char prog[] = "C:\\ALIENS\\ALIENSP2.EXE";
    memcpy(MEM + pg, prog, sizeof prog);
    WR32(W_PGMNAME, pg);
    WR32(W_STACKLOW, pg + sizeof prog);
    WR16(W_NO87, 0);
    WR32(W_STACKTOP, sp);
    rt_load_seg(S_ES, SEL_DATA);   /* flat model from here on: es = ds */
    cpu.r[R_ESP] = sp;
    cpu.r[R_EBP] = 0;
    cpu.r[R_EAX] = 0xff;
    rt_call_guest(W_INITRTNS);
    cpu.r[R_EBP] = 0;
    rt_call_guest(W_CMAIN);
}

int main(int argc, char **argv)
{
    char exe[600];
    GetModuleFileNameA(NULL, exe, sizeof exe);
    char *s = strrchr(exe, '\\');
    if (s) *s = 0;
    snprintf(rt_root, sizeof rt_root, "%s\\game", exe);
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--root") && i + 1 < argc) snprintf(rt_root, sizeof rt_root, "%s", argv[++i]);
    char logp[700];
    snprintf(logp, sizeof logp, "%s\\aliens.log", exe);
    rt_logf = fopen(logp, "w");
    rt_log("Aliens: A Comic Book Adventure - PC native runtime\nroot: %s\n", rt_root);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) rt_fatal("SDL: %s", SDL_GetError());
    mem_init();
    dpmi_init();
    dos_init(rt_root);
    { extern void settings_load(const char *); settings_load(exe); }
    video_init();

    char exep[700];
    snprintf(exep, sizeof exep, "%s\\hd\\ALIENS\\ALIENSP2.EXE", rt_root);
    LoadInfo li;
    if (!le_load(exep, &li))
        rt_fatal("Could not load %s\nRun the installer first (it needs your Aliens CDs and the 1.03 update).", exep);
    rt_log("loaded: entry %08x stack %08x\n", li.entry, li.stack);

    /* game arguments (e.g. 'p' = copy files) */
    char args[128] = "";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--root")) { i++; continue; }
        if (strlen(args) + strlen(argv[i]) + 2 < sizeof args) {
            if (args[0]) strcat(args, " ");
            strcat(args, argv[i]);
        }
    }
    setup_process(args);

    memset(&cpu, 0, sizeof cpu);
    FPU_INIT();
    cpu.r[R_ESP] = li.stack;
    rt_load_seg(S_CS, SEL_CODE);
    rt_load_seg(S_DS, SEL_DATA);
    rt_load_seg(S_SS, SEL_DATA);
    rt_load_seg(S_FS, 0);
    rt_load_seg(S_GS, 0);
    rt_load_seg(S_ES, SEL_PSP);
    cpu.r[R_ESP] -= 4;
    WR32(cpu.r[R_ESP], MAGIC_IRET + 0xfe);
    native_cstart(li.stack, args);
    rt_log("program returned from entry\n");
    return 0;
}

/* function tracing (tools/trace.txt) */
void rt_trace_enter(u32 f)
{
    rt_log("> %08x eax=%08x edx=%08x ebx=%08x ecx=%08x esi=%08x edi=%08x [esp+4]=%08x [esp+8]=%08x\n", f,
           cpu.r[0], cpu.r[2], cpu.r[3], cpu.r[1], cpu.r[6], cpu.r[7], RD32(cpu.r[4] + 4), RD32(cpu.r[4] + 8));
}

void rt_trace_leave(u32 f)
{
    rt_log("< %08x eax=%08x edx=%08x\n", f, cpu.r[0], cpu.r[2]);
}
