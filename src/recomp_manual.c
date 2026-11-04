/**
 * Panzer Dragoon Orta - manual function overrides and ICALL diagnostics.
 *
 * Origin: sp00nznet/xboxrecomp templates/new-game/src/recomp_manual.c
 *         (commit 193e299, MIT), see docs/sources.md.
 *
 * Rule (CLAUDE.md): bugs are fixed in the lifter, never in generated C.
 * Workarounds go here as overrides, each with a comment: why, address,
 * how it was verified.
 *
 * This file provides:
 *   - recomp_lookup_manual()  : intercept specific Xbox VAs with hand-written code
 *   - recomp_icall_fail_log() : log when an indirect call target can't be resolved
 *   - ICALL trace ring buffer  : globals used by the RECOMP_ICALL macro
 *
 * The recomp pipeline generates an auto-dispatch table (recomp_lookup) that
 * resolves most function addresses. recomp_lookup_manual() is called FIRST,
 * giving you a chance to override any function with a custom implementation.
 *
 * Common reasons to add manual overrides:
 *   - Trace a function to understand call flow (wrap the generated version)
 *   - Fix a function the lifter translated incorrectly
 *   - Stub out a function that crashes (return early, set eax to a safe value)
 *   - Redirect a function to a native implementation (e.g., skip CRT init)
 *   - Intercept D3D/audio calls for custom rendering or sound
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>   /* strlen (options labels) */

/* ── ICALL trace ring buffer ───────────────────────────────── */

/*
 * These globals are written by the RECOMP_ICALL macro (defined in
 * recomp_types.h) every time an indirect call is dispatched. When a
 * crash occurs, the VEH handler or recomp_icall_fail_log() can dump
 * the last 16 call targets to help you trace what happened.
 *
 * The runtime owns them: xbox_kernel defines all three in
 * src/kernel/xbox_memory_layout.c, and recomp_types.h declares them extern.
 * Declare, do not define -- a definition here as well is a duplicate symbol,
 * and a project copied from this template failed to link on all three:
 *
 *   xbox_memory_layout.obj : error LNK2005: g_icall_count already defined
 *                            in recomp_manual.obj
 */
extern volatile uint32_t g_icall_trace[16];
extern volatile uint32_t g_icall_trace_idx;
extern uint32_t recomp_icall_trace_at(unsigned back);   /* toolkit: the calling thread's ring */
extern volatile uint64_t g_icall_count;

/* recomp_func_t: void(X86Ctx *ctx, uint8_t *base) - xbox_memory_layout.h / recomp_types.h */

/* ── Register state ──────────────────────────────────────────
 * Option C: every function here gets the caller's register set as X86Ctx *ctx
 * (overrides as void sub_X(RECOMP_PARAMS)); g_eax, g_esp ... name the fields
 * of that ctx (xbox_memory_layout.h). There is no global register set. */
#include "xbox_memory_layout.h"
#include <stddef.h>
extern ptrdiff_t g_xbox_mem_offset;
#define GUEST32(va) (*(volatile uint32_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))

/* ── Manual function overrides ─────────────────────────────── */

/*
 * Return a function pointer to override the given Xbox VA, or NULL
 * to fall through to the auto-generated dispatch table.
 *
 * This is called on every indirect call (RECOMP_ICALL) and every
 * direct call through the dispatch table, so keep it fast. A chain
 * of if-statements on uint32_t compiles to a simple comparison
 * sequence; for large override tables, consider a sorted array
 * with binary search.
 *
 * Examples of common override patterns:
 *
 *   // Trace wrapper: log entry/exit around the generated function
 *   extern void sub_00012345(void);
 *   static void traced_sub_00012345(void) {
 *       fprintf(stderr, "[TRACE] sub_00012345 entered, eax=0x%08X\n", g_eax);
 *       sub_00012345();
 *       fprintf(stderr, "[TRACE] sub_00012345 returned, eax=0x%08X\n", g_eax);
 *   }
 *
 *   // Stub: skip a function entirely (return 0 in eax)
 *   static void stub_00067890(void) {
 *       g_eax = 0;
 *   }
 *
 *   // Fix: replace a broken lifted function with correct C
 *   static void fixed_sub_000ABCDE(void) {
 *       // Read arguments from stack/registers per calling convention
 *       uint32_t arg1 = g_ecx;
 *       uint32_t arg2 = MEM32(g_esp + 4);
 *       // ... correct implementation ...
 *       g_eax = result;
 *   }
 */
recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    /*
     * TODO: Add your overrides here. Examples:
     *
     * if (xbox_va == 0x00012345) return traced_sub_00012345;
     * if (xbox_va == 0x00067890) return stub_00067890;
     * if (xbox_va == 0x000ABCDE) return fixed_sub_000ABCDE;
     */

    (void)xbox_va;
    return (recomp_func_t)0;
}

/* ── ICALL failure logging ─────────────────────────────────── */

/*
 * Called when RECOMP_ICALL cannot resolve a target address.
 * This usually means one of:
 *   - A vtable dispatch to an address not in the dispatch table
 *   - A function pointer loaded from uninitialized or corrupt memory
 *   - A kernel thunk address that the bridge doesn't handle
 *
 * During early bring-up you will see many of these. Most are harmless
 * (the ICALL macro pops the dummy return address and continues).
 * Focus on the ones that cause crashes or incorrect behavior.
 */
void recomp_icall_fail_log(X86Ctx *ctx, uint32_t va)
{
    /* Rate-limited per address: a missing vtable target is called every frame
     * (0x0011F760 wrote 850,000 log lines in two minutes). The first three
     * with the call history, then the count at each power of two. */
    static uint32_t seen_va[32];
    static unsigned seen_n[32];
    unsigned k, n = 0;
    for (k = 0; k < 32 && seen_va[k] && seen_va[k] != va; k++)
        ;
    if (k < 32) {
        seen_va[k] = va;
        n = ++seen_n[k];
    }
    if (n > 3) {
        if ((n & (n - 1)) == 0)
            fprintf(stderr, "[ICALL] Failed to resolve VA 0x%08X: %u times so far\n", va, n);
        return;
    }
    fprintf(stderr, "[ICALL] Failed to resolve VA 0x%08X (total calls: %llu)\n",
            va, (unsigned long long)g_icall_count);

    /* Dump last 16 call targets from the ring buffer */
    fprintf(stderr, "  Recent ICALL targets:\n");
    for (int i = 0; i < 16; i++) {
        uint32_t va_i = recomp_icall_trace_at(15u - (unsigned)i);
        if (va_i)
            fprintf(stderr, "    [%2d] 0x%08X\n", i, va_i);
    }
    fflush(stderr);
}

/* An indirect call whose target is not code: a null or wild function pointer.
 *
 * Skipping these is right -- calling a data address is worse -- but skipping
 * them *silently* is not. They almost always arrive inside a loop, so the
 * symptom is a hang with no output rather than a diagnosable null vtable call.
 *
 * Rate-limited per address: a spin can produce millions of these, and the
 * useful information is which addresses occur, not how often.
 */
void recomp_icall_not_code_log(X86Ctx *ctx, uint32_t va)
{
    enum { SLOTS = 16 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS];
    static int count;
    int i;

    for (i = 0; i < count; i++)
        if (seen[i] == va)
            break;
    if (i == count) {
        if (count == SLOTS)
            return;
        seen[count] = va;
        hits[count] = 0;
        count++;
    }
    hits[i]++;
    /* Report at 1, 10, 100, 1000 ... rather than once. A single line says a
     * wild pointer was skipped; the progression says it is being skipped in a
     * loop, which is the difference between a curiosity and the reason the
     * title is hung. */
    {
        uint64_t n = hits[i];
        while (n >= 10 && n % 10 == 0)
            n /= 10;
        if (n != 1)
            return;
    }
    /* The guest return address is still on top of the guest stack here:
     * RECOMP_ICALL pops it only after this returns. It names the call site,
     * which the target (0 for a null pointer) never does. */
    fprintf(stderr, "[ICALL] target 0x%08X is not code -- skipped %llu time(s) "
                    "(null or wild function pointer, at call #%llu, "
                    "returning to 0x%08X)\n",
            va, (unsigned long long)hits[i],
            (unsigned long long)g_icall_count,
            g_esp ? GUEST32(g_esp) : 0u);
    fflush(stderr);
}

/* ── Untranslated instructions ───────────────────────────────────────────
 *
 * The lifter emits RECOMP_UNIMPL(text, va) at every instruction it has no
 * translation for, in place of the bare comment it used to leave. The
 * instruction is still a no-op; this only stops the omission being silent.
 * RECOMP_UNIMPL_TRAP=1 aborts at the first hit, at the guest address of the
 * cause rather than wherever the damage surfaces. */
#include <stdlib.h>

void recomp_unimpl(X86Ctx *ctx, const char *text, uint32_t va)
{
    static int printed;
    const char *trap = getenv("RECOMP_UNIMPL_TRAP");
    int stop = trap && *trap && *trap != '0';

    if (printed < 50 || stop) {
        printed++;
        fprintf(stderr,
                "[UNIMPL] untranslated instruction REACHED: `%s` at 0x%08X"
                " (a no-op; set RECOMP_UNIMPL_TRAP=1 to stop here)\n",
                text, va);
        fflush(stderr);
    }
    if (stop) abort();
}

/* ── Game-specific overrides go below ───────────────────────────────────────
 *
 * Rule: fix bugs in the lifter, never in generated C. Workarounds here as
 * wrappers (define sub_XXXXXXXX and call sub_XXXXXXXX_gen; the lift renames the
 * generated body) with a comment: why, address, how it was verified.
 * First one every title needs (M2): the D3D fence wait -- register the GPU
 * semaphore with nv2a_pb_set_semaphore_target (see the THPS 2X port,
 * sub_00019FE0, and porting_guide.md). */

/* ── Title addresses (Europe/PAL default.xbe 57213FF3...) ─────────────────
 *
 * Every guest address this file uses, in one place (2026-11-01: the
 * toolkit's per-version address headers will replace this block once a second
 * version is built into the same exe; US equivalents from the T1 map
 * of the US -> PAL address map in the comments). Overrides are defined by
 * their PAL name below; keep VA literals out of the code. */
#define ORTA_D3D_DEVICE_PTR   0x002BB788u   /* g_pDevice (US 0x002BD708) */
#define ORTA_DEV_SEM_PTR_OFF  0x30u         /* device+0x30 -> GPU semaphore (what the GPU consumed) */
/* XLaunchNewImage = sub_0022DA5C (US 0x0022CE3C), wrapped below by its PAL name. */

/* ── D3D fence wait (BlockOnTime), sub_002AFF90 (no US match: XDK 4928 vs 5028) ─
 *
 * XDK 5028 D3D: reads [g_pDevice+0x30] (-> semaphore the GPU writes), compares
 * with [g_pDevice+0x2C] (what the title submitted) and waits until the GPU has
 * caught up. Seen as the hang of run 20261101_113648_m1_wd: device release
 * sub_002AA8F0 -> sub_002B1220 -> sub_002B02B0 -> here, forever (no flip in
 * 40 s, GET == PUT). The fork's pushbuffer executor writes NV2A semaphore
 * releases (method 0x1D70) to the title's real semaphore once everything
 * before them has executed; it only needs the address, once per device.
 * Same pattern as Burnout 3 bench sub_00351770 / THPS 2X sub_00019FE0.
 * Without RECOMP_PB_EXEC this is the generated function unchanged.
 *
 * Wait sleeping, not spinning (profile, 2026-11-03): the attract demo used 0.85-0.9 host cores against 0.40
 * for the reference port (efficiency run 20261103_195256). A sampling profile of the busiest thread (run
 * 20261103_203806_B_sample2) put the main thread 83 % of the time here and in KickOff (sub_002AFD90): BlockOnTime
 * spins in loc_002B0090 reading the GPU semaphore until it has caught up with the caller's target. On the console
 * that ends within the GPU's frame time; here the GPU is the pushbuffer executor on another host thread, and the
 * main thread burnt a whole core meanwhile. The generated exit test (0x2AFF90..0x2AFFB2): done when
 * (put - target) >= (put - *sem), unsigned, put = [dev+0x2C], target = the first stack argument. target == put is
 * left alone (the XDK kicks the pushbuffer off itself first, sub_002AFEE0). Otherwise sleep 0.25 ms per check
 * (host_sleep_ns: high-resolution waitable timer, no spin), at most 250 ms; the generated code then finds the wait
 * satisfied (or takes over as before). */
#include "platform/host_wait.h"   /* host_sleep_ns (toolkit) */
extern void nv2a_pb_set_semaphore_target(uint32_t guest_va);
extern void sub_002AFF90_gen(RECOMP_PARAMS);

void sub_002AFF90(RECOMP_PARAMS)
{
    static int exec = -1;
    static uint32_t registered;
    uint32_t dev = GUEST32(ORTA_D3D_DEVICE_PTR);
    uint32_t sem = dev ? GUEST32(dev + ORTA_DEV_SEM_PTR_OFF) : 0;

    if (exec < 0)
        exec = getenv("RECOMP_PB_EXEC") != NULL && *getenv("RECOMP_PB_EXEC") != '\0';
    if (exec && sem && sem != registered) {
        nv2a_pb_set_semaphore_target(sem);
        fprintf(stderr, "[D3D] GPU semaphore at 0x%08X\n", sem);
        registered = sem;
    }
    if (exec && sem) {
        uint32_t target = GUEST32(g_esp + 4);       /* [esp] is the return address */
        uint32_t put = GUEST32(dev + 0x2C);
        int i;
        if (target != put)
            for (i = 0; i < 1000 && (uint32_t)(put - target) < (uint32_t)(put - GUEST32(sem)); i++)
                host_sleep_ns(250000ull);
    }
    sub_002AFF90_gen(ctx, base);
}

/* ── CRI idle-counter thread, sub_00234930: count while sleeping, not spinning (profile, 2026-11-03) ──────────
 *
 * Why: in movie phases the port used 1.0-1.3 host cores against ~0.85 for the reference port (efficiency run
 * 20261103_215401). A sampling profile of the busiest thread during the snow intro movie (run
 * 20261103_223056_B_profmovie2) had one thread 100 % in here: a worker the CRI middleware starts with the movie
 * streams (thread routine 0x00234930, one of the four ctx 0x002349xx workers):
 *
 *     L: inc dword [0x487D28]        ; idle count
 *        cmp dword [0x487D40], 0     ; quit flag
 *        je  L
 *        mov dword [0x487D44], 1     ; "ended"
 *        PsTerminateSystemThread(0xF0000001)
 *
 * The middleware's idle meter: on the console it runs at the lowest priority and only gets time nobody else wants,
 * so the count measures idle time. Here it has a host core of its own and burnt all of it. Now it counts once per
 * host millisecond of sleep (host_sleep_ns, no spin) until the quit flag is set; then the generated code runs on and
 * ends the thread exactly as before. The count's rate stays steady, so whatever the middleware derives from it
 * reads the same throughout. To check with every change here: movies still play smoothly (the count could feed
 * CRI's frame-skip logic). */
extern void sub_00234930_gen(RECOMP_PARAMS);

void sub_00234930(RECOMP_PARAMS)
{
    while (GUEST32(0x00487D40u) == 0) {
        GUEST32(0x00487D28u) = GUEST32(0x00487D28u) + 1;
        host_sleep_ns(1000000ull);
    }
    sub_00234930_gen(ctx, base);   /* sees the flag set: marks itself ended and terminates */
}

/* ── XLaunchNewImage(lpTitlePath, pLaunchData), sub_0022DA5C, stdcall 8 ──
 *
 * XAPI: a path "D:\..." launches that XBE (quick reboot with the launch data
 * page filled); NULL returns to the dashboard. Orta calls it
 *   - for the Sega Saturn bonus game in Pandora's Box: "d:\pd1.xbe" from
 *     sub_000121C0 (launch data magic 'PDLD' 0x50444C44) - the only caller,
 *   - with NULL when main() ends (game init failed -> dashboard; M1 runs).
 * Saturn bonus game (maintainer 2026-11-01): not planned. orta_launch_saturn_bonus()
 * tells the player so and the call returns ERROR_NOT_SUPPORTED instead of rebooting,
 * so the title stays in its menu. That function is the one place a later, separate
 * Saturn recomp would be started from (e.g. run its exe and wait for it).
 * Every other path: logged, then the original. To check in M3 once Pandora's Box is
 * reachable: what the menu does after the call returns (it never returns on the console). */
extern void sub_0022DA5C_gen(RECOMP_PARAMS);
#include "platform/host.h"
#define ORTA_ERROR_NOT_SUPPORTED 50u

/* The Sega Saturn "Panzer Dragoon" in Pandora's Box. Returns 1 if it ran (never, for now). */
static int orta_launch_saturn_bonus(void)
{
    host_notice("The Sega Saturn bonus game (Panzer Dragoon) is not planned for this recomp.\n\n"
                "Das Sega-Saturn-Bonusspiel (Panzer Dragoon) ist in diesem Recomp nicht geplant.");
    return 0;
}

static int orta_path_is(const char *p, const char *want)
{
    for (; *p && *want; p++, want++) {
        char a = *p, b = *want;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (a == '/') a = '\\';
        if (a != b)
            return 0;
    }
    return *p == 0 && *want == 0;
}

void sub_0022DA5C(RECOMP_PARAMS)
{
    uint32_t ret = GUEST32(g_esp);
    uint32_t path_va = GUEST32(g_esp + 4);
    uint32_t data_va = GUEST32(g_esp + 8);
    char path[64];
    unsigned i = 0;

    if (path_va)
        for (; i < sizeof(path) - 1; i++) {
            char c = (char)(*(volatile uint8_t *)((uintptr_t)g_xbox_mem_offset + path_va + i));
            if (!c)
                break;
            path[i] = c;
        }
    path[i] = 0;
    fprintf(stderr, "[LAUNCH] XLaunchNewImage('%s', data 0x%08X) from 0x%08X\n",
            path_va ? path : "(NULL = dashboard)", data_va, ret);
    {
        unsigned k;
        fprintf(stderr, "[LAUNCH] guest stack:");
        for (k = 0; k < 48; k++)
            fprintf(stderr, "%s%08X", (k % 8) ? " " : "\n    ", GUEST32(g_esp + k * 4));
        fprintf(stderr, "\n");
    }
    fflush(stderr);
    if (path_va && orta_path_is(path, "d:\\pd1.xbe")) {
        if (!orta_launch_saturn_bonus()) {
            fprintf(stderr, "[LAUNCH] Saturn bonus game not planned - XLaunchNewImage returns ERROR_NOT_SUPPORTED\n");
            g_eax = ORTA_ERROR_NOT_SUPPORTED;
            g_esp += 4 + 8;                  /* ret 8 */
            return;
        }
    }
    sub_0022DA5C_gen(ctx, base);
}

/* Deferred file-I/O completion APCs: toolkit option-c 1545a76 (RECOMP_APC_DEFERRED=1, set in main.c) - the
 * port probe that found it (orta_recomp 676eeff..85d4052) is removed. Why Orta needs it: the CRI file layer's
 * sub_0003F0A0 clears its request's done flag right after ReadFileEx / WriteFileEx return and waits with
 * SleepEx(1, TRUE) for the completion sub_0003E840 to set it - an inline APC is lost (hang after START). */

/* DSOUND's idle-voice trap handler (sub_002C2F33) runs as generated: the false idle report for its internal
 * submix voice 0x40 came from garbage voice state the APU read from the XBE image before toolkit cb833c7 (owner
 * table); the port guard (85d4052) is removed - run 20261101_185434_m2_noguard: 0 idle traps for 0x40, no crash. */

/* ── DIAG: Sofdec/ADX 6-channel volume set-up, sub_0025AC60(obj) (US 0x0025A340?) ─
 * Crash 20261101_152140_m2_tkapc: sub_0025AB00 (called only through here, cdecl,
 * eax = obj) reads 6 float volumes from [obj+0x2C][i] and writes each as a 0x80-byte
 * fill into the buffer [[obj+0x30]][i] - that buffer pointer was 0x50545CA5. Log
 * the object the first times (ORTA_DIAG_ADXVOL=1); no behaviour change. */
extern void sub_0025AC60_gen(RECOMP_PARAMS);

void sub_0025AC60(RECOMP_PARAMS)
{
    static int on = -1;
    static unsigned logged;
    if (on < 0) {
        const char *e = getenv("ORTA_DIAG_ADXVOL");
        on = e && *e == '1';
    }
    if (on && logged < 12) {
        uint32_t obj = GUEST32(g_esp + 4);
        uint32_t vols = GUEST32(obj + 0x2C), bufs = GUEST32(obj + 0x30);
        logged++;
        fprintf(stderr, "[ADXVOL] obj %08X flags %08X %04X vols %08X bufs %08X +48 %08X | ret %08X\n",
                obj, GUEST32(obj), GUEST32(obj + 4) & 0xFFFF, vols, bufs, GUEST32(obj + 0x48), GUEST32(g_esp));
        if (bufs && bufs < 0x08000000u)
            fprintf(stderr, "[ADXVOL]   bufs[0..5] %08X %08X %08X %08X %08X %08X\n",
                    GUEST32(bufs), GUEST32(bufs + 4), GUEST32(bufs + 8), GUEST32(bufs + 12),
                    GUEST32(bufs + 16), GUEST32(bufs + 20));
        if (vols && vols < 0x08000000u)
            fprintf(stderr, "[ADXVOL]   vols[0..5] %08X %08X %08X %08X %08X %08X\n",
                    GUEST32(vols), GUEST32(vols + 4), GUEST32(vols + 8), GUEST32(vols + 12),
                    GUEST32(vols + 16), GUEST32(vols + 20));
    }
    sub_0025AC60_gen(ctx, base);
}

/* ── DIAG: Sofdec MPEG VLC callback sub_0025E9A0 (cdecl, called via [dec+0x12A8]) ─
 * The caller (0x2F79A0) keeps a pointer in ebx across six calls of this callback
 * and the callback must preserve ebx/esi/edi/ebp (it pushes ebx, ebp, esi, edi on
 * entry). After them ebx was 0x50545CA5 (run 20261101_153849_m2_adx). The
 * disassembler split this hand-written decoder into dozens of overlapping
 * tail_jump_alias functions (0x25E95F..0x25E9A3, 2 bytes apart). This wrapper
 * checks the callee-saved registers and esp across the call (ORTA_DIAG_VLC=1). */
extern void sub_0025E9A0_gen(RECOMP_PARAMS);

void sub_0025E9A0(RECOMP_PARAMS)
{
    static int on = -1;
    static unsigned logged;
    uint32_t b = g_ebx, s = g_esi, d = g_edi, p = g_ebp, sp = g_esp, ret = GUEST32(g_esp);
    if (on < 0) {
        const char *e = getenv("ORTA_DIAG_VLC");
        on = e && *e == '1';
    }
    sub_0025E9A0_gen(ctx, base);
    if (on && logged < 16 && (g_ebx != b || g_esi != s || g_edi != d || g_ebp != p || g_esp != sp + 4)) {
        logged++;
        fprintf(stderr, "[VLC] callee-saved changed (ret %08X): ebx %08X->%08X esi %08X->%08X edi %08X->%08X"
                        " ebp %08X->%08X esp %08X->%08X (expect +4)\n",
                ret, b, g_ebx, s, g_esi, d, g_edi, p, g_ebp, sp, g_esp);
    }
}

/* ── Pad read clock: XAPI XInputOpen / XInputGetState (XPP, all stdcall) ─────────
 * Addresses: PAL 0x0030917C XInputOpen(type, port, slot, params) ret 0x10 and 0x003093BC XInputGetState(h, state)
 * ret 8 - the functions the reference port SeveerJ/Orta_PC_Recomp hooks on the US build (recomp_manual.c, read as
 * a reference), found in the PAL XBE through the US -> PAL address map (both "exact").
 * Why: under the USB pad model the OHCI controller fetches reports on its own schedule (also during loads), so a
 * pad script / recording timed in "reads" needs the title's own reads. Toolkit xbox_InputTitleRead(port) (option-c,
 * pad recorder) counts them: called once per XInputGetState for the port the handle was opened on.
 * XInputOpen notes handle -> port; behaviour of both functions is unchanged (the generated code runs). */
#include "xinput_xbox.h"   /* toolkit: xbox_InputTitleRead */

extern void sub_0030917C_gen(RECOMP_PARAMS);
extern void sub_003093BC_gen(RECOMP_PARAMS);

static uint32_t orta_pad_handle[4];

void sub_0030917C(RECOMP_PARAMS)
{
    uint32_t port = GUEST32(g_esp + 8);
    sub_0030917C_gen(ctx, base);
    if (port < 4 && g_eax)
        orta_pad_handle[port] = g_eax;
}

void sub_003093BC(RECOMP_PARAMS)
{
    uint32_t h = GUEST32(g_esp + 4);
    for (int p = 0; p < 4; p++) {
        if (h && orta_pad_handle[p] == h) {
            xbox_InputTitleRead(p);
            break;
        }
    }
    sub_003093BC_gen(ctx, base);
}

/* ── Movie teardown race guards (stop-gap; RE 2026-11-03) ─────────────
 *
 * Why: on the console one CPU runs every thread by priority, and Orta's movie code raises its CRI stream thread to
 * TIME_CRITICAL around its work (~80 toggles a second, KeSetBasePriorityThread increment 16 / 0). Here every guest
 * thread has a host core of its own, so the main thread tears a movie down (sub_0023B300 -> DSOUND sub_002BF5A7:
 * the stream's voice released, stream->[0x20] = 0) while the stream worker is still in its service path. A real
 * fix needs a one-CPU scheduler in the toolkit (opt-in, later); the toolkit's time-critical gate (1dede0d) did not
 * help (2 of 3 runs crashed). Until then: the two places the worker dereferences what the teardown freed fail
 * the way the title's own code already fails there, logged the 1st, 2nd, 4th, 8th ... time with the count. The
 * counters are only read and written by the stream worker (one thread), so they need no lock.
 *
 * Evidence: runs 39, 49, B 20261102_020035 / _020658 (37 s, 2 of 2), O 20261102_022611_gate_2 (site 1);
 * O 20261102_022648_gate_3 (site 2).
 *
 * Site 1: sub_002C6319 (DSOUND, `this` in ecx, 1 stack arg, ret 4). Its only caller here, the CDirectSoundStream
 * method sub_002BEA98, takes the DSOUND lock (sub_002BE20D), loads ecx = stream->[0x20] (the stream's voice) and
 * calls it; [ecx+0x12] faulted with ecx = 0 (fault 0x12, edx = stream 0x4B82E0). The check runs inside that lock,
 * so it cannot race the release. ecx = 0: E_FAIL (0x80004005), which sub_002BEA98 returns as its result - the
 * same value its own early-failure path (0x2BEAB9) returns. The caller leaves the lock itself afterwards.
 *
 * Site 2: sub_0023C8F0 (CRI sound output service, cdecl 1 arg = the output object, result unused by its caller
 * 0x23CEA3). It loads ebx = obj->[8] (the sound buffer object) and calls [[ebx]+0x20]; with the vtable pointer
 * [ebx] = 0 (object released) that read faulted at 0x20 (ebx 0x4BE5E0, esi 0x4B7520). No buffer or no vtable:
 * skip this one service call (return, nothing touched). */
extern void sub_002C6319_gen(RECOMP_PARAMS);
extern void sub_0023C8F0_gen(RECOMP_PARAMS);

/* ── CRI server pass vs. CRI destroy: one at a time (RE 2026-11-03; host calls via platform/) ──
 *
 * Why: the movie-teardown race. The stream worker (thread routine 0x234970 -> sub_00237910 -> sub_002378B0 ->
 * server function pointer) runs one CRI server pass in sub_00236D90; every crash stack of the race goes through it
 * (0x236D9A: runs 39, 49, B 20261102_020035, O gate_2/gate_3, B 20261103_113202_B_fl30). The main thread destroys
 * the same objects in sub_00236F60 (reached through a function pointer, return address 0x2456A1):
 * ORTA_WATCH_WRITE=4BE5E0 (run 20261103_113357_B_watch) shows it zeroing the output buffer object 0x4BE5E0 (vtable
 * 0x3F6A88, constructed by sub_00237410) on the main thread while a pass was using it - B_fl30 crashed in
 * sub_0023C8F0 between its two virtual calls on that object, past the entry guard. On the console the stream thread
 * runs its pass at TIME_CRITICAL on the one CPU, so the destroy never lands inside a pass; here both run on their
 * own cores. So both take one host lock: a destroy waits for the running pass to end, a pass waits for a running
 * destroy. Both take it before anything else; inside both the title only takes its own locks (DSOUND's) after it,
 * so the order is the same on both sides. A wait over 500 ms goes on without the lock, logged - a stall, never a
 * hang. ORTA_CRI_LOCK=0: off. The mutex is made in main.c (orta_cri_lock_init) before the title's first thread. */
extern void sub_00236D90_gen(RECOMP_PARAMS);
extern void sub_00236F60_gen(RECOMP_PARAMS);

static void *s_cri_lock;

void orta_cri_lock_init(void)
{
    const char *e = getenv("ORTA_CRI_LOCK");
    if (e && *e == '0')
        return;
    s_cri_lock = host_mutex_new();
    fprintf(stderr, "[CRI] server pass and destroy take one lock (ORTA_CRI_LOCK=0: off)\n");
}

static int cri_lock_take(const char *who)
{
    static unsigned late;              /* bumped by both threads at most; only a log throttle */
    if (!s_cri_lock)
        return 0;
    if (!host_mutex_take(s_cri_lock, 500)) {
        unsigned n = ++late;
        if (!(n & (n - 1)))
            fprintf(stderr, "[CRI] %s waited 500 ms for the CRI lock, goes on without it (%u times)\n", who, n);
        return 0;
    }
    return 1;
}

void sub_00236D90(RECOMP_PARAMS)
{
    int held = cri_lock_take("server pass");
    sub_00236D90_gen(ctx, base);
    if (held)
        host_mutex_give(s_cri_lock);
}

void sub_00236F60(RECOMP_PARAMS)
{
    int held = cri_lock_take("destroy");
    sub_00236F60_gen(ctx, base);
    if (held)
        host_mutex_give(s_cri_lock);
}

/* ── The same lock for EVERY CRI server level and for the construct (RE 2026-11-04) ─────────────────────────
 *
 * Why: the rare movie-START crash (ref_port_20261103_192903\orta_crash.log, 1 in all runs of 2026-11-03): a worker
 * in the Sofdec decoder sub_002F9620 (0x2F9CC5) called [[0x4BE4A0]+0x20] with the vtable 0. Watch run
 * 20261104_034256_B_watch4BE4A0: 0x4BE4A0 is the same object family as 0x4BE5E0 - built by sub_00237410, destroyed
 * by sub_00236F60 on the main thread - but the crashing pass ran through another server level: the CRI server
 * loop sub_002378B0(level) runs the table 0x487DC8 + level*32 (4 x func+arg); sub_00237910 runs level 2,
 * sub_00237920 level 4, sub_00237930 level 5, and only the level-2 server sub_00236D90 took the lock. Now the whole
 * level loop takes it (every level, every thread) and so does the construct. host_mutex is a Win32 mutex: the same
 * thread may take it again (nesting with sub_00236D90 inside the loop is fine); order unchanged (CRI lock first,
 * the title's own locks after it); a wait over 500 ms goes on without it, logged. */
extern void sub_002378B0_gen(RECOMP_PARAMS);
extern void sub_00237410_gen(RECOMP_PARAMS);

void sub_002378B0(RECOMP_PARAMS)
{
    int held = cri_lock_take("server level loop");
    sub_002378B0_gen(ctx, base);
    if (held)
        host_mutex_give(s_cri_lock);
}

/* Forcing hook for the movie-start race ([[hook]] at 0x2F9CBB in config\variants\pal.toml): sleeps
 * ORTA_RACE_SOFDEC_MS before the decoder's virtual call on 0x4BE4A0, so a destroy on the main thread can land in
 * between. Off unless the variable is set; logged once. */
void orta_race_sofdec_delay(void)
{
    static int ms = -1;
    if (ms < 0) {
        const char *e = getenv("ORTA_RACE_SOFDEC_MS");
        ms = e ? atoi(e) : 0;
        if (ms > 0) fprintf(stderr, "[RACE] forcing test: %d ms before the Sofdec call at 0x2F9CC5\n", ms);
    }
    if (ms > 0)
        host_sleep_ms((unsigned)ms);
}

void sub_00237410(RECOMP_PARAMS)
{
    int held = cri_lock_take("construct");
    sub_00237410_gen(ctx, base);
    if (held)
        host_mutex_give(s_cri_lock);
}

/* ORTA_RACE_GUARDS=0: both guards off (straight to the generated code) - A/B against the forcing test below. */
static int race_guards_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("ORTA_RACE_GUARDS");
        on = !(e && *e == '0');
    }
    return on;
}

/* DEBUG forcing test (2026-11-03): ORTA_RACE_DELAY_MS=<n> makes the stream worker sleep n ms inside the CRI server
 * pass, right before the output service sub_0023C8F0 reads the buffer object - so a destroy on the main thread
 * (sub_00236F60) lands inside the pass: without the CRI lock and guards it crashes, with the lock the destroy waits.
 * (A first version slept after the voice release at 0x23B32B and never forced it: runs 20261103_115103..115535.)
 * Off (0) unless set; logged once when it first applies. */
static void race_force_delay(void)
{
    static int delay = -1;
    if (delay < 0) {
        const char *e = getenv("ORTA_RACE_DELAY_MS");
        delay = e ? atoi(e) : 0;
        if (delay > 0)
            fprintf(stderr, "[RACE] forcing test: the CRI output service sleeps %d ms before each call\n", delay);
    }
    if (delay > 0)
        host_sleep_ms((unsigned)delay);
}

void sub_002C6319(RECOMP_PARAMS)
{
    if (!race_guards_on()) {
        sub_002C6319_gen(ctx, base);
        return;
    }
    if (g_ecx == 0) {
        static unsigned hits;
        if (!(++hits & (hits - 1)))     /* the 1st, 2nd, 4th, 8th ... time: how often it fires */
            fprintf(stderr, "[RACE] stream voice gone: DSOUND sub_002C6319 with this = NULL (eax 0x%08X edx 0x%08X),"
                            " E_FAIL as the caller's own failure path (%u times)\n", g_eax, g_edx, hits);
        g_eax = 0x80004005u;
        g_esp += 8;                     /* ret 4: return address + the argument */
        return;
    }
    sub_002C6319_gen(ctx, base);
}

void sub_0023C8F0(RECOMP_PARAMS)
{
    uint32_t obj = GUEST32(g_esp + 4);
    uint32_t buf = obj ? GUEST32(obj + 8) : 0;
    race_force_delay();
    if (!race_guards_on()) {
        sub_0023C8F0_gen(ctx, base);
        return;
    }
    buf = obj ? GUEST32(obj + 8) : 0;   /* again after the forcing delay */
    if (!buf || !GUEST32(buf)) {
        static unsigned hits;
        if (!(++hits & (hits - 1)))
            fprintf(stderr, "[RACE] stream voice gone: CRI sub_0023C8F0 output 0x%08X buffer 0x%08X has no vtable,"
                            " service call skipped (%u times)\n", obj, buf, hits);
        g_esp += 4;                     /* cdecl: the caller pops the argument */
        return;
    }
    sub_0023C8F0_gen(ctx, base);
}

/* XAPI GetExitCodeThread (sub_0022E138) runs as generated since toolkit option-c 2d7da49: thread
 * objects for ObReferenceObjectByHandle + a host-handle token fix. The port probe that worked around the NULL
 * object (91d1d4d) is removed. */

/* ── Options menu labels are text in the game's sprite font (2026-11-04) ──────
 *
 * The OPTIONEN line labels are not pictures: each label record of the loaded pdx_option*.spr leads to a text node
 * {+0x0C count, +0x10 -> dword glyph codes}; letters A-Z are ASCII + 0x6A, '/' 0x86, '.' 0x85, '-' 0x3C,
 * ' ' 0x10F, 'Ä' 0x2E1 (decoded from opt_snap 20261103_233130). Record list: [[0x0047E084] + 8], 28-byte nodes
 * {id, data, .., next at +0x18}; label text nodes at [[data + 0x54] + 0] and [[data + 0x58] + 0x40].
 *
 * First step of the maintainer's layout: the line AUF/AB is renamed AIMING ("auf/ab ergibt keinen Sinn"). Both
 * have six glyphs, so the text is rewritten in place after the options screen has loaded its sprites
 * (sub_000AF410, the screen's open function - call census: first called when OPTIONEN opens). A label whose text
 * is not the stock one (another language, already patched) is left alone. ORTA_OPT_LABELS=0 turns it off. */
extern void sub_000AF410_gen(RECOMP_PARAMS);
static int opt_views_on(void);
static void opt_apply_view(void);

static int opt_text_is(uint32_t node, const uint32_t *codes, uint32_t n)
{
    uint32_t c = GUEST32(node + 0x0C), s = GUEST32(node + 0x10), i;
    if (c != n || !s) return 0;
    for (i = 0; i < n; i++) if (GUEST32(s + 4 * i) != codes[i]) return 0;
    return 1;
}

static uint32_t opt_glyph(char ch)
{
    if (ch >= 'A' && ch <= 'Z') return (uint32_t)ch + 0x6A;
    /* Digits and ':' have no stock use in the menus. ASCII+0x6A crashed the quad draw (run 20261104_074634_opt_views2,
     * sub_00069990 with a garbage handle under 0x1B93A0): not in the font. Space until the real codes are known. */
    switch (ch) { case '/': return 0x86; case '.': return 0x85; case '-': return 0x3C; default: return 0x10F; }
}

/* Rewrite every label text node equal to `from` (n_from glyphs) into `to` (ASCII, at most n_from glyphs). */
static int opt_relabel(const uint32_t *from, uint32_t n_from, const char *to)
{
    uint32_t owner = GUEST32(0x0047E084u), n = owner ? GUEST32(owner + 8) : 0, hits = 0, guard = 0;
    uint32_t len = (uint32_t)strlen(to);
    if (len > n_from) return 0;
    for (; n && guard < 4096; n = GUEST32(n + 0x18), guard++) {
        uint32_t data = GUEST32(n + 4), k, node, i, s;
        static const uint32_t slot[2] = { 0x54, 0x58 }, sub[2] = { 0x00, 0x40 };
        int j;
        if (!data) continue;
        for (j = 0; j < 2; j++) {
            if (!(k = GUEST32(data + slot[j])) || !(node = GUEST32(k + sub[j]))) continue;
            if (!opt_text_is(node, from, n_from)) continue;
            s = GUEST32(node + 0x10);
            for (i = 0; i < len; i++) GUEST32(s + 4 * i) = opt_glyph(to[i]);
            GUEST32(node + 0x0C) = len;
            hits++;
        }
    }
    return (int)hits;
}

/* Runs from a [[hook]] at 0x000AF4FA (config\variants\pal.toml): inside sub_000AF410 right after the table-5
 * sprite load sub_001B7330(5) and BEFORE the seven sub_001BB7C0 calls that create the persistent value / dot
 * instances - which copy their position once (run 20261104_043102_opt_compact: shifted values stayed put). So
 * text and positions are changed while nothing has been instantiated yet. The flags are dead there (the next
 * instruction loads eax and compares). */
void orta_opt_after_load(void)
{
    static const uint32_t auf_ab[6] = { 'A' + 0x6A, 'U' + 0x6A, 'F' + 0x6A, 0x86, 'A' + 0x6A, 'B' + 0x6A };
    const char *e = getenv("ORTA_OPT_LABELS");
    if (e && *e == '0') return;
    {
        int n = opt_relabel(auf_ab, 6, "AIMING");
        static int told;
        if (!told) { told = 1; fprintf(stderr, "[OPT] options labels: AUF/AB -> AIMING (%d text nodes)\n", n); }
    }
    if (opt_views_on()) opt_apply_view();
}

void sub_000AF410(RECOMP_PARAMS)
{
    sub_000AF410_gen(ctx, base);   /* the work is done by the hook orta_opt_after_load inside it */
}

/* ── Options views, step 1 of the build plan: hide stock lines, cursor skips them ─────
 *
 * A hidden line keeps its slot but its label texts get count 0 (nothing drawn); the input handler sub_000AF580
 * moves the cursor (+0x88) as before and is run again while the cursor sits on a hidden line, with only the same
 * up/down bit in +0x18, so the game's own sound / redraw / help text follow. Prototype: ORTA_OPT_VIEWS=1 hides
 * CONTROLLERKONF. (line 2, the presets are dropped) and VIDEOEINSTELL. (line 7, moves into RECOMP OPTIONS);
 * off by default until RECOMP OPTIONS exists. Stock label record ids per line: 0x0036BEDC (normal) /
 * 0x0036BF24 (selected); text nodes as in opt_relabel. */
extern void sub_000AF580_gen(RECOMP_PARAMS);

static uint32_t s_opt_hidden;   /* bit per stock line 0..8 */

/* The maintainer's layout (views) is the default since 2026-11-04 ~11:00 (RECOMP OPTIONS working);
 * ORTA_OPT_VIEWS=0 shows the stock nine lines (with AIMING) for comparisons. */
static int opt_views_on(void)
{
    const char *e = getenv("ORTA_OPT_VIEWS");
    return !(e && *e == '0');
}

/* Set the glyph count of every label text node of record `id` (count 0 hides; the original is kept in +0x08 of
 * the text node, which is 0 in every stock label, so it can be restored). */
static void opt_label_count(uint32_t id, int hide)
{
    uint32_t owner = GUEST32(0x0047E084u), n = owner ? GUEST32(owner + 8) : 0, guard = 0;
    for (; n && guard < 4096; n = GUEST32(n + 0x18), guard++) {
        uint32_t data, k, node;
        static const uint32_t slot[2] = { 0x54, 0x58 }, sub[2] = { 0x00, 0x40 };
        int j;
        if (GUEST32(n) != id || !(data = GUEST32(n + 4))) continue;
        for (j = 0; j < 2; j++) {
            if (!(k = GUEST32(data + slot[j])) || !(node = GUEST32(k + sub[j]))) continue;
            if (hide && GUEST32(node + 0x0C)) { GUEST32(node + 0x08) = GUEST32(node + 0x0C); GUEST32(node + 0x0C) = 0; }
            if (!hide && !GUEST32(node + 0x0C) && GUEST32(node + 0x08)) {
                GUEST32(node + 0x0C) = GUEST32(node + 0x08); GUEST32(node + 0x08) = 0;
            }
        }
    }
}

/* Move every position word of record `id` that lies in the row band of `old_y` by `dy`. Positions are packed
 * x | y << 16 words in the record's blocks (label, its selected twin, values, cursor bar 24 px above the row, the
 * volume dots); found in opt_snap 20261103_233130 by following the record's pointers up to depth 3. The band
 * [old_y - 26, old_y + 5] holds this row's bar (-24), values (-10..-1) and labels / dots (0) but not the next row's
 * bar (+6). A block is visited once per call. */
/* Two hash sets per line move (open addressing, cleared by a generation stamp): blocks already walked, and words
 * already moved. The 64-dword windows of different block starts overlap, so a word reachable through two starts
 * must still move once only (run 20261104_065901_opt_compact4: with a per-start set only, some dots moved twice). */
#define OPT_SET_SLOTS 65536u
static uint32_t s_set_key[2][OPT_SET_SLOTS], s_set_gen[2][OPT_SET_SLOTS], s_gen = 1;
static uint8_t s_set_depth[OPT_SET_SLOTS];   /* set 0 only: the smallest depth a block was walked at */

/* Set 1 (moved words): 1 = newly added, 0 = was there (or table full). Set 0 (walked blocks): 1 = walk it now -
 * new, or reached again at a SMALLER depth (run 20261104_070146_B_optsnap: dots 3 and 7 of every row were first
 * reached at depth 3 through a neighbour's window, where their children are not followed, and the later depth-2
 * visit from their own sibling root was refused - their keys never moved). */
static int opt_set_add(int which, uint32_t key, int depth)
{
    uint32_t h = (key * 2654435761u) & (OPT_SET_SLOTS - 1u), probe;
    for (probe = 0; probe < 64; probe++, h = (h + 1u) & (OPT_SET_SLOTS - 1u)) {
        if (s_set_gen[which][h] != s_gen) {
            s_set_gen[which][h] = s_gen; s_set_key[which][h] = key;
            if (which == 0) s_set_depth[h] = (uint8_t)depth;
            return 1;
        }
        if (s_set_key[which][h] == key) {
            if (which == 0 && depth < s_set_depth[h]) { s_set_depth[h] = (uint8_t)depth; return 1; }
            return 0;
        }
    }
    return 0;
}

/* x_lo..x_hi: the column filter of the move mode (labels / bar left of OPT_VALUE_X, values right of it). */
static uint32_t s_walk_x_lo = 21, s_walk_x_hi = 639;

static int opt_shift_walk(uint32_t p, int depth, uint32_t old_y, int dy)
{
    int i, n = 0;
    if (depth > 3 || p < 0x00400000u || p >= 0x04000000u || !opt_set_add(0, p, depth)) return 0;
    for (i = 0; i < 64; i++) {
        uint32_t a = p + 4 * (uint32_t)i, v = GUEST32(a), x = v & 0xFFFFu, y = v >> 16;
        /* below the row: +12 for the value column (volume dots' wave keys go to +8), only +5 left of it - the next
         * row's cursor bar sits at +6 and went along (line 8's bar ended in row 2: runs 20261104_*_opt_views20,
         * 20261104_103651_opt_bind1) */
        if (x > 20 && x < 640 && y + 26 >= old_y && y <= old_y + (x < 200u /* OPT_VALUE_X */ ? 5u : 12u) && y >= 20) {
            if (x >= s_walk_x_lo && x <= s_walk_x_hi && opt_set_add(1, a, 0)) {
                GUEST32(a) = x | ((uint32_t)((int)y + dy) << 16); n++;
            }
        } else if (v >= 0x00400000u && v < 0x04000000u) {
            n += opt_shift_walk(v, depth + 1, old_y, dy);
        }
    }
    return n;
}

/* Move stock line `line` (row y 46 + 30*line) by `rows` rows: its labels, cursor bar, value sprites, volume dots.
 * All records of the loaded options sprite set, not a hand list: the first try (run 20261104_051201) moved the
 * labels and bars but not the per-level value records (the full volume dot rows, the ANZEIGE value 0x513..0x515).
 * Only words inside the row band move, so the other rows' records are untouched even when walked. The option
 * screen's own records are ids 0x123..0x77E; the controller / video sub-screens' records (0x177.., 0x438..) sit at
 * other rows of their own screens but are walked too - the band keeps their words unless their y falls in it, so
 * ids 0x170..0x1FF and 0x430..0x44F (sub-screens) are skipped. */
/* mode: OPT_MV_ALL every position word of the row; OPT_MV_LABEL only words left of OPT_VALUE_X (labels at x ~50,
 * the cursor bar); OPT_MV_VALUE only words right of it (value boxes from x ~218, volume dots 380..580) - so a view
 * can show a line's label without its value (RECOMP OPTIONS: "NAME: VALUE" texts on stock lines). A filter by
 * record id did not work: the label records reach the value keys through shared blocks, so parking the labels took
 * the values along (run 20261104_075300_opt_views4, vv05820: NORMAL / EIN still beside RECOMP lines 0 and 1). */
enum { OPT_MV_ALL, OPT_MV_LABEL, OPT_MV_VALUE };
#define OPT_VALUE_X 200u

static int opt_shift_line(int line, int from_row, int to_row, int mode)
{
    uint32_t y = 46 + 30 * (uint32_t)from_row, owner = GUEST32(0x0047E084u), n = owner ? GUEST32(owner + 8) : 0, guard = 0;
    int dy = 30 * (to_row - from_row), moved = 0;
    if (dy == 0) return 0;
    if (++s_gen == 0) s_gen = 1;   /* new generation = both sets empty */
    s_walk_x_lo = mode == OPT_MV_VALUE ? OPT_VALUE_X : 21u;
    s_walk_x_hi = mode == OPT_MV_LABEL ? OPT_VALUE_X - 1u : 639u;
    for (; n && guard < 4096; n = GUEST32(n + 0x18), guard++) {
        uint32_t id = GUEST32(n), data = GUEST32(n + 4), sib;
        int links;
        int other_bar = 0, b;
        if (!data || (id >= 0x170 && id <= 0x1FF) || (id >= 0x430 && id <= 0x44F)) continue;
        /* The band reaches +12 below the row (the volume dots' wave keys go 15 above to 8 below their row, run
         * 20261104_071551_opt_compact5), which covers the NEXT row's cursor bar (24 above that row = +6 here): the
         * other lines' bar records (0x0036BF00[]) are left out of this line's move. */
        for (b = 0; b < 9; b++) if (b != line && GUEST32(0x0036BF00u + 4 * (uint32_t)b) == id) other_bar = 1;
        if (other_bar) continue;
        moved += opt_shift_walk(data, 0, y, dy);
        /* Sibling chain of sprite objects off the data block (+0x0C next, +0x10 prev; run
         * 20261104_065050_B_watchdots): a volume dot row is 11 such objects (x 380..580, step 20), each with its
         * own position key two levels below - walked as new roots so the depth limit does not cut the row. */
        for (sib = GUEST32(data + 0x0C), links = 0; sib >= 0x00400000u && sib < 0x04000000u && links < 64;
             sib = GUEST32(sib + 0x0C), links++)
            moved += opt_shift_walk(sib, 0, y, dy);
    }
    return moved;
}

static int opt_clone_label(uint32_t tmpl_id, uint32_t new_id, const char *text, uint32_t x, uint32_t y);

/* ── The menu views (the maintainer's final layout) ─────────────────────────────────────────
 *
 * The game's screen knows 9 stock lines (index 0..8, cursor +0x88 = line index, values per line). A view decides
 * which stock line shows in which row and with which text; every other line is parked far below the screen (row
 * 40 + line, unique, so no two lines ever share a row band while moving). No new records are needed for these
 * views: the four lines with no value or the right value are reused - 2 (CONTROLLERKONF.), 6 (ANZEIGE SICHTBAR),
 * 7 (VIDEOEINSTELL.), 8 (VERLASSEN).
 *   TOP:      ANZEIGE SICHTBAR (6), SOUND (7), CONTROLS (2), RECOMP OPTIONS (8)    - VERLASSEN dropped, B leaves
 *   CONTROLS: AIMING (0), VIBRATION (1), BUTTON MAPPING (2)
 *   SOUND:    BGM / SE / STIMMEN volume (3, 4, 5)
 * Input (sub_000AF580 override): A (+0x18 bit mask 0x101) on CONTROLS / SOUND switches the view, on RECOMP OPTIONS
 * and BUTTON MAPPING it is swallowed (not built yet); B (0x202) in a sub-view goes back to TOP; B in TOP is the
 * game's own: saves the values into 0x34274C.. and leaves. Up / down skip the parked lines.
 * Texts: a view's text for a line replaces the glyph pointer + count of that line's label text nodes (normal,
 * selected, plain row records) with a port-owned glyph buffer; the stock values are restored for lines without a
 * view text. All state is rebuilt on every open (orta_opt_after_load), the records are reloaded then. */
/* Views from OPT_V_RECOMP on are the port's own: their lines come from k_pl (value lines, sub-lists, picks). */
enum { OPT_V_TOP, OPT_V_CONTROLS, OPT_V_SOUND, OPT_V_RECOMP, OPT_V_AA, OPT_V_FPS, OPT_V_BOOT, OPT_V_BIND,
       OPT_V_RESTART, OPT_V_PIC, OPT_V_N };
static const signed char k_opt_rows[OPT_V_N][9] = {
    /* line:   0   1   2   3   4   5   6   7   8 */
    {         -1, -1,  2, -1, -1, -1,  0,  1,  3 },   /* TOP: ANZEIGE, SOUND, CONTROLS, RECOMP (maintainer ~08:00) */
    {          0,  1,  2, -1, -1, -1, -1, -1, -1 },   /* CONTROLS */
    {         -1, -1, -1,  0,  1,  2, -1, -1, -1 },   /* SOUND */
    {          0,  1,  2,  3,  4,  5,  6,  7,  8 },   /* RECOMP OPTIONS: every stock line, value sprites parked */
    {          0,  1,  2,  3,  4, -1, -1, -1, -1 },   /* RECOMP > ANTIALIASING: the five choices */
    {          0,  1,  2, -1, -1, -1, -1, -1, -1 },   /* RECOMP > FRAME RATE: FRAME RATE, VSYNC, FPS COUNTER */
    {          0,  1, -1, -1, -1, -1, -1, -1, -1 },   /* RECOMP > BOOT ANIMATION: status, ON / OFF */
    {          0,  1,  2,  3,  4,  5,  6,  7,  8 },   /* CONTROLS > BUTTON MAPPING: DEVICE, RESET, 6 bindings, NEXT */
    {          0,  1,  2, -1, -1, -1, -1, -1, -1 },   /* RESTART NEEDED: the line, RESTART NOW, LATER */
    {          0,  1, -1, -1, -1, -1, -1, -1, -1 },   /* RECOMP > BRIGHTNESS AND CONTRAST: the game's two values */
};
/* views whose lines show their stock value sprites (others park them, "NAME: VALUE" is in the text) */
static const unsigned char k_opt_values[OPT_V_N] = { 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
static const char *const k_opt_text[OPT_V_N][9] = {
    { 0, 0, "CONTROLS", 0, 0, 0, 0, "SOUND", "RECOMP OPTIONS" },
    { "AIMING", 0, "BUTTON MAPPING", 0, 0, 0, 0, 0, 0 },
    { 0 },
};
static int s_opt_view = OPT_V_TOP;
static int s_opt_row[9];
static int s_opt_vrow[9];       /* the row of each line's VALUE sprites (parked at 60 + line when a view hides them) */
static int s_opt_need_cursor;   /* after an open: put the cursor on TOP's first line at the first handler call */

/* Stock text (count, glyph pointer) of a record's text node - never patched (see opt_line_text). */
static void opt_node_stock(uint32_t node, uint32_t *count, uint32_t *glyphs)
{
    *count = GUEST32(node + 0x0C); *glyphs = GUEST32(node + 0x10);
}

/* ── RECOMP OPTIONS values (model: THPS 2X src/recomp_manual.c s_val / thps_options_apply; CLAUDE.md "RECOMP
 * OPTIONS") ─────────────────────────────────────────────────────────────────────────────────────────────────────
 * The view's stock lines 0..8 carry: DISPLAY MODE, RENDER RESOLUTION, ASPECT RATIO, ANISOTROPIC FILTER,
 * ANTIALIASING >, FRAME RATE >, BOOT ANIMATION >, LANGUAGE, BRIGHTNESS (CONTRAST and scrolling past 9 lines: open).
 * Left / right step a value line, save it at once (saves\settings.ini) and apply it where the toolkit can at run
 * time (display mode, render scale, anisotropic filter); ASPECT RATIO and LANGUAGE apply at the next start (main.c
 * reads them before the device / XAPI settings exist). The sub-lists (">") and BRIGHTNESS are not built yet. */
#include "settings.h"
#include <math.h>
#include <stdarg.h>
extern volatile int nv2a_vk_aniso;
void nv2a_vk_set_render_scale(double factor, int wide);
int  nv2a_vk_render_scale_fits(double factor, int wide);
void nv2a_vk_set_display_mode(int mode);
void nv2a_vk_set_antialiasing(int mode);
enum { ORTA_VK_DISPLAY_WINDOWED, ORTA_VK_DISPLAY_BORDERLESS, ORTA_VK_DISPLAY_FULLSCREEN };  /* = NV2A_VK_DISPLAY_* */

void nv2a_vk_set_fps_hud(int mode);
int  nv2a_vk_antialiasing_fits(int mode);

typedef struct { const char *label, *key, *def; const char *val[6], *txt[6]; int n; } opt_val_t;
enum { RV_DISPLAY, RV_RENDER, RV_ASPECT, RV_ANISO, RV_LANGUAGE, RV_AA, RV_FRAMERATE, RV_VSYNC, RV_FPSHUD, RV_BOOT,
       RV_N };
static const opt_val_t k_rv[RV_N] = {
    /* index = NV2A_VK_AA_*; applied at once if the device can (nv2a_vk_antialiasing_fits), else refused */
    [RV_AA]       = { "ANTIALIASING", "antialiasing", "fxaa", { "off", "fxaa", "smaa", "msaa2", "msaa4" },
                      { "OFF", "FXAA", "SMAA", "MSAA 2X", "MSAA 4X" }, 5 },
    /* 120 is M7 (after M6, maintainer 2026-11-04): ORIGINAL only until then */
    [RV_FRAMERATE] = { "FRAME RATE", "frame_rate", "original", { "original" }, { "ORIGINAL 60" }, 1 },
    [RV_VSYNC]    = { "VSYNC", "vsync", "on", { "on", "off" }, { "ON", "OFF" }, 2 },      /* main.c, at start */
    [RV_FPSHUD]   = { "FPS COUNTER", "fps_counter", "off", { "off", "fps", "fps_frametime", "frametime" },
                      { "OFF", "FPS", "FPS AND FRAME TIME", "FRAME TIME" }, 4 },        /* = NV2A_VK_HUD_* */
    [RV_BOOT]     = { "BOOT ANIMATION", "boot_animation", "on", { "on", "off" }, { "ON", "OFF" }, 2 },
    [RV_DISPLAY]  = { "DISPLAY MODE", "display_mode", "borderless", { "fullscreen", "borderless", "windowed" },
                      { "FULLSCREEN", "BORDERLESS", "WINDOWED" }, 3 },
    [RV_RENDER]   = { "RENDER RESOLUTION", NULL, NULL, { 0 }, { 0 }, 0 },          /* opt_render_* */
    [RV_ASPECT]   = { "ASPECT RATIO", "aspect_ratio", "4:3", { "4:3", "16:9" }, { "4:3", "16:9" }, 2 },
    [RV_ANISO]    = { "ANISOTROPIC FILTER", "anisotropic_filter", "16", { "off", "2", "4", "8", "16" },
                      { "OFF", "2X", "4X", "8X", "16X" }, 5 },
    /* SYSTEM = the host's UI language if the disc has it (toolkit xbox_settings, else English) - as before this line */
    [RV_LANGUAGE] = { "LANGUAGE", "language", "system",
                      { "system", "english", "german", "french", "spanish", "italian" },
                      { "SYSTEM", "ENGLISH", "DEUTSCH", "FRANCAIS", "ESPANOL", "ITALIANO" }, 6 },
};
/* The port views' lines: a value line (left / right / Y), a sub-list (A opens view a), a pick (A sets value b of
 * k_rv[a]), a plain text. */
enum { PL_NONE, PL_VAL, PL_SUB, PL_PICK, PL_TEXT, PL_GAMEF };   /* PL_GAMEF: a game float, a = 0 / 1 below */
typedef struct { unsigned char kind, a, b; const char *text; } opt_pl_t;
static const opt_pl_t k_pl[OPT_V_N][9] = {
    [OPT_V_RECOMP] = { { PL_VAL, RV_DISPLAY }, { PL_VAL, RV_RENDER }, { PL_VAL, RV_ASPECT }, { PL_VAL, RV_ANISO },
                       { PL_SUB, OPT_V_AA, 0, "ANTIALIASING" }, { PL_SUB, OPT_V_FPS, 0, "FRAME RATE" },
                       { PL_SUB, OPT_V_BOOT, 0, "BOOT ANIMATION" }, { PL_VAL, RV_LANGUAGE },
                       { PL_SUB, OPT_V_PIC, 0, "BRIGHTNESS AND CONTRAST" } },
    [OPT_V_AA]     = { { PL_PICK, RV_AA, 0 }, { PL_PICK, RV_AA, 1 }, { PL_PICK, RV_AA, 2 }, { PL_PICK, RV_AA, 3 },
                       { PL_PICK, RV_AA, 4 } },
    [OPT_V_FPS]    = { { PL_VAL, RV_FRAMERATE }, { PL_VAL, RV_VSYNC }, { PL_VAL, RV_FPSHUD } },
    [OPT_V_BOOT]   = { { PL_TEXT, 1 }, { PL_VAL, RV_BOOT } },   /* a = 1: the file's status line */
    /* leaving RECOMP OPTIONS with a restart-bound value changed (CLAUDE.md RESTART NEEDED; THPS 2X host_restart) */
    [OPT_V_RESTART] = { { PL_TEXT, 0, 0, "RESTART NEEDED" }, { PL_TEXT, 3, 0, "RESTART NOW" },
                        { PL_TEXT, 4, 0, "LATER" } },
    [OPT_V_PIC]    = { { PL_GAMEF, 0, 0, "BRIGHTNESS" }, { PL_GAMEF, 1, 0, "CONTRAST" } },
};

/* The game's gamma ramp on screen: sub_002AA6A0 = the XDK's D3DDevice_SetGammaRamp(Flags, pRamp) (lifted), called
 * by the game's gamma object (sub_0027B430: push ramp, push 1) when BRIGHTNESS / CONTRAST change. On the console the
 * RAMDAC applies the ramp; here the device state only stores it, so the ramp also goes to the toolkit's present pass
 * (nv2a_vk_set_gamma_ramp, option-c 56c0efc; identity = no pass, RECOMP_VK_GAMMA=0 off). D3DGAMMARAMP on the Xbox =
 * BYTE red[256], green[256], blue[256] (verified: the function copies 0xC0 dwords). */
extern void sub_002AA6A0_gen(RECOMP_PARAMS);
void nv2a_vk_set_gamma_ramp(const uint8_t r[256], const uint8_t g[256], const uint8_t b[256]);
void sub_002AA6A0(RECOMP_PARAMS)
{
    uint32_t ramp = GUEST32(g_esp + 8);                       /* [esp+4] = Flags, [esp+8] = pRamp */
    sub_002AA6A0_gen(ctx, base);
    if (ramp >= 0x00010000u && ramp < 0x04000000u) {
        const uint8_t *p = (const uint8_t *)((uintptr_t)g_xbox_mem_offset + ramp);
        nv2a_vk_set_gamma_ramp(p, p + 0x100, p + 0x200);
    }
}

/* ── BRIGHTNESS / CONTRAST: the game's own values (2026-11-04, static + snapshot): floats in the settings block,
 * 0x342768 = HELLIGKEIT (i - 5) * 0.06, 0x342764 = KONTRAST (i - 5) * 0.1, i = 0..10, start / STANDARDEINSTELLUNG
 * i = 5 (0.0). sub_0002A9B0 applies a change by itself the next frame (it caches the last values), and the block
 * 0x34274C.. goes into the game's save (sub_000121C0) - nothing else to call or store. Shown as -5..+5. */
static const uint32_t k_gamef_va[2] = { 0x00342768u, 0x00342764u };
static const float k_gamef_step[2] = { 0.06f, 0.1f };
static int gamef_index(int which)
{
    uint32_t bits = GUEST32(k_gamef_va[which]);
    float v;
    int i;
    memcpy(&v, &bits, 4);
    i = (int)lroundf(v / k_gamef_step[which]) + 5;
    return i < 0 ? 0 : i > 10 ? 10 : i;
}
static void gamef_set(int which, int i)
{
    float v = (float)(i - 5) * k_gamef_step[which];
    uint32_t bits;
    memcpy(&bits, &v, 4);
    GUEST32(k_gamef_va[which]) = bits;
}

/* ── BOOT ANIMATION (porting guide; model THPS 2X thps_boot_video): media\boot.mpg (MPEG-1 + MP2) plays before the
 * title when boot_animation is on (default); it cannot be skipped. A boot.mp4 / boot.mkv without boot.mpg is only
 * named (log, status line) - the port never converts or changes anything in media\. */
char g_orta_media_dir[512];
#if defined(ORTA_BOOTVIDEO)
#include "boot_video.h"
int  nv2a_vk_boot_begin(void);
int  nv2a_vk_boot_frame(const uint8_t *bgra, int w, int h);
void nv2a_vk_boot_end(void);
static int boot_sink(const uint8_t *bgra, int w, int h, void *user) { (void)user; return nv2a_vk_boot_frame(bgra, w, h); }
#endif

/* the status line's text; 1 when media\boot.mpg is there */
static int boot_state(char *path, size_t n, char *status, size_t sn)
{
#if defined(ORTA_BOOTVIDEO)
    int convert = 0, have = g_orta_media_dir[0] ? boot_video_find(g_orta_media_dir, path, n, &convert) : 0;
    /* at most ~34 glyphs: 17 px each from x 50 in a 640 px screen */
    snprintf(status, sn, "%s", have ? "BOOT.MPG FOUND" : convert ? "CONVERT BOOT VIDEO  SEE README"
                                                                 : "NEEDS BOOT MPG IN THE MEDIA FOLDER");
    return have;
#else
    (void)path; (void)n;
    snprintf(status, sn, "NOT IN THIS BUILD");
    return 0;
#endif
}

/* main.c, after the renderer is installed and before the title starts */
void orta_boot_video(void)
{
#if defined(ORTA_BOOTVIDEO)
    char path[700], status[64];
    BootVideoSink sink = { boot_sink, NULL };
    if (!boot_state(path, sizeof path, status, sizeof status)) {
        if (status[0] == 'C')
            fprintf(stderr, "[BOOT] media\\boot.mp4 or boot.mkv found, but no boot.mpg - convert it once (README); the "
                            "port does not convert or change files in media\\\n");
        return;
    }
    if (!strcmp(settings_get("boot_animation", "on"), "off")) {
        fprintf(stderr, "[BOOT] %s found, BOOT ANIMATION is off\n", path);
        return;
    }
    if (!nv2a_vk_boot_begin()) return;
    boot_video_play(path, &sink);
    nv2a_vk_boot_end();
#endif
}
static const opt_pl_t *opt_pl(int view, int line)
{
    return view >= OPT_V_RECOMP && view < OPT_V_N && line >= 0 && line < 9 ? &k_pl[view][line] : NULL;
}

/* restart-bound values (ASPECT RATIO, LANGUAGE, VSYNC): the index this process started with, -1 for the others -
 * a saved value that differs shows " - RESTART" (CLAUDE.md: hints sit in the value; "AFTER RESTART" made
 * "ASPECT RATIO: 16:9 AFTER RESTART" wider than the screen, run 20261104_*_opt_views20) */
static int s_rv_start[RV_N] = { [0 ... RV_N - 1] = -1 };

/* DISPLAY MODE for this session only (index into k_rv[RV_DISPLAY], -1 = the saved one): ORTA_START_WINDOWED=1
 * (tools/play.ps1) starts the maintainer's sessions windowed WITHOUT RECOMP_DISPLAY_MODE, which the toolkit lets win
 * over every menu change - so FULLSCREEN / BORDERLESS did nothing in his session (2026-11-04 15:00). A step or Y on
 * the line ends the override. */
static int s_dm_session = -1;

static int rv_index(int id)
{
    if (id == RV_DISPLAY && s_dm_session >= 0) return s_dm_session;
    const char *cur = settings_get(k_rv[id].key, k_rv[id].def);
    int i;
    for (i = 0; i < k_rv[id].n; i++) if (!strcmp(cur, k_rv[id].val[i])) return i;
    for (i = 0; i < k_rv[id].n; i++) if (!strcmp(k_rv[id].def, k_rv[id].val[i])) return i;
    return 0;
}

/* RENDER RESOLUTION: 4:3 = 1.00X..5.00X in 0.25 steps; 16:9 = the standard sizes (CLAUDE.md). The aspect is the one
 * running (read at start), each keeps its own key. */
static const int k_wide_lines[] = { 480, 720, 900, 1080, 1440, 1800, 2160, 2400 };
static int s_ws_start = -1;
static int rv_ws(void)
{
    if (s_ws_start < 0) s_ws_start = !strcmp(settings_get("aspect_ratio", "4:3"), "16:9");
    return s_ws_start;
}
static int rv_render_last(int ws) { return ws ? 7 : 16; }
static double rv_render_factor(int s, int ws) { return ws ? k_wide_lines[s] / 480.0 : 1.0 + 0.25 * s; }
static const char *rv_render_key(int ws) { return ws ? "render_scale_wide" : "render_scale"; }
static int rv_render_step(int ws, int *saved)
{
    const char *v = settings_get(rv_render_key(ws), NULL);
    int s, best = 0;
    if (saved) *saved = v != NULL;
    if (!v) return 0;
    for (s = 1; s <= rv_render_last(ws); s++)
        if (fabs(rv_render_factor(s, ws) - atof(v)) < fabs(rv_render_factor(best, ws) - atof(v))) best = s;
    return best;
}

static int rv_aniso_level(void)
{
    int n = atoi(settings_get("anisotropic_filter", "16"));
    return n == 2 || n == 4 || n == 8 || n == 16 ? n : 1;
}

static int rv_display_mode(void)
{
    static const int m[3] = { ORTA_VK_DISPLAY_FULLSCREEN, ORTA_VK_DISPLAY_BORDERLESS, ORTA_VK_DISPLAY_WINDOWED };
    return m[rv_index(RV_DISPLAY)];
}

/* the value a line applies at once (0 = restart-bound or saved only) */
static void rv_apply(int id)
{
    if (id == RV_DISPLAY) nv2a_vk_set_display_mode(rv_display_mode());
    if (id == RV_ANISO) nv2a_vk_aniso = rv_aniso_level();
    if (id == RV_AA) nv2a_vk_set_antialiasing(rv_index(RV_AA));
    if (id == RV_FPSHUD) nv2a_vk_set_fps_hud(rv_index(RV_FPSHUD));
}

/* the text of a port view line into out (upper case, the menu font's letters; no '>' in the font) */
static void rv_text(int view, int line, char *out, size_t n)
{
    const opt_pl_t *pl = opt_pl(view, line);
    int id = pl && pl->kind == PL_VAL ? pl->a : -1;
    if (pl && pl->kind == PL_PICK) {
        snprintf(out, n, "%s%s", k_rv[pl->a].txt[pl->b], rv_index(pl->a) == pl->b ? " - ON" : "");
        return;
    }
    if (pl && pl->kind == PL_TEXT && pl->a == 1) {           /* BOOT ANIMATION's status line */
        char path[700];
        boot_state(path, sizeof path, out, n);
        return;
    }
    if (pl && pl->kind == PL_GAMEF) {                        /* the font has '-' but no '+' */
        snprintf(out, n, "%s: %d", pl->text, gamef_index(pl->a) - 5);
        return;
    }
    if (id < 0) { snprintf(out, n, "%s", pl && pl->text ? pl->text : ""); return; }
    if (id == RV_RENDER) {
        /* the resolution only, no factor (maintainer 2026-11-04 15:00): 4:3 = 640 x 480 times the factor, 16:9 =
         * the standard sizes (lines x 16/9) */
        int ws = rv_ws(), s = rv_render_step(ws, NULL);
        double f = rv_render_factor(s, ws);
        int h = (int)(480 * f + 0.5), w = ws ? (int)(h * 16.0 / 9.0 + 0.5) : (int)(640 * f + 0.5);
        snprintf(out, n, "%s: %dX%d", k_rv[id].label, w, h);
        return;
    }
    snprintf(out, n, "%s: %s%s", k_rv[id].label, k_rv[id].txt[rv_index(id)],
             s_rv_start[id] >= 0 && s_rv_start[id] != rv_index(id) ? " - RESTART" : "");   /* ~34 glyphs fit */
}

/* A on a pick line: that value, saved and applied (ANTIALIASING refuses what the device cannot hold). 1 = changed */
static int rv_pick(int view, int line)
{
    const opt_pl_t *pl = opt_pl(view, line);
    if (!pl || pl->kind != PL_PICK || rv_index(pl->a) == pl->b) return 0;
    if (pl->a == RV_AA && !nv2a_vk_antialiasing_fits(pl->b)) return 0;
    settings_set(k_rv[pl->a].key, k_rv[pl->a].val[pl->b]);
    rv_apply(pl->a);
    return 1;
}

/* left (-1) / right (+1) on a value line: step, save, apply. 1 = a value changed. */
static int rv_step(int view, int line, int dir)
{
    const opt_pl_t *pl = opt_pl(view, line);
    int id = pl && pl->kind == PL_VAL ? pl->a : -1, i;
    if (pl && pl->kind == PL_GAMEF) {                        /* the game applies it next frame (sub_0002A9B0) */
        i = gamef_index(pl->a) + dir;
        if (i < 0 || i > 10) return 0;
        gamef_set(pl->a, i);
        return 1;
    }
    if (id < 0) return 0;
    if (id == RV_RENDER) {
        int ws = rv_ws(), s = rv_render_step(ws, NULL) + dir;
        char v[16];
        if (s < 0 || s > rv_render_last(ws)) return 0;
        if (!nv2a_vk_render_scale_fits(rv_render_factor(s, ws), ws)) return 0;   /* refused: the device cannot hold it */
        snprintf(v, sizeof v, "%.3f", rv_render_factor(s, ws));
        settings_set(rv_render_key(ws), v);
        nv2a_vk_set_render_scale(rv_render_factor(s, ws), ws);
        return 1;
    }
    i = rv_index(id) + dir;
    if (i < 0 || i >= k_rv[id].n) return 0;                     /* ends stop, as the stock value lines */
    if (id == RV_DISPLAY) s_dm_session = -1;                     /* the player's choice from now on */
    settings_set(k_rv[id].key, k_rv[id].val[i]);
    rv_apply(id);
    return 1;
}

/* Y on a value line or in a pick list: back to the default, saved and applied. 1 = a value changed. */
static int rv_default(int view, int line)
{
    const opt_pl_t *pl = opt_pl(view, line);
    int id = pl && (pl->kind == PL_VAL || pl->kind == PL_PICK) ? pl->a : -1;
    if (pl && pl->kind == PL_GAMEF) {                        /* STANDARDEINSTELLUNG: index 5 = 0.0 */
        if (gamef_index(pl->a) == 5) return 0;
        gamef_set(pl->a, 5);
        return 1;
    }
    if (id < 0) return 0;
    if (id == RV_RENDER) {
        int ws = rv_ws();
        if (rv_render_step(ws, NULL) == 0) return 0;
        settings_set(rv_render_key(ws), "1.000");
        nv2a_vk_set_render_scale(1.0, ws);
        return 1;
    }
    if (id == RV_DISPLAY && s_dm_session >= 0) {                 /* Y while the session forces WINDOWED */
        s_dm_session = -1;
        settings_set(k_rv[id].key, k_rv[id].def);
        rv_apply(id);
        return 1;
    }
    if (!strcmp(settings_get(k_rv[id].key, k_rv[id].def), k_rv[id].def)) return 0;
    settings_set(k_rv[id].key, k_rv[id].def);
    rv_apply(id);
    return 1;
}

/* a restart-bound value saved that is not the running one (RESTART NEEDED when leaving RECOMP OPTIONS) */
static int rv_restart_pending(void)
{
    int id;
    for (id = 0; id < RV_N; id++)
        if (s_rv_start[id] >= 0 && s_rv_start[id] != rv_index(id)) return 1;
    return 0;
}

/* main.c, right after nv2a_vk_install: the saved values the renderer takes at run time */
void orta_options_apply(void)
{
    int saved, ws = rv_ws(), s = rv_render_step(ws, &saved);
    if (saved) nv2a_vk_set_render_scale(rv_render_factor(s, ws), ws);
    {
        extern int host_was_restarted(void);   /* platform/host.h */
        const char *w = getenv("ORTA_START_WINDOWED");
        if (w && *w == '1' && !host_was_restarted()) s_dm_session = 2;   /* WINDOWED for this session */
    }
    s_rv_start[RV_ASPECT] = rv_index(RV_ASPECT);
    s_rv_start[RV_LANGUAGE] = rv_index(RV_LANGUAGE);
    s_rv_start[RV_VSYNC] = rv_index(RV_VSYNC);
    rv_apply(RV_DISPLAY);
    rv_apply(RV_ANISO);
    rv_apply(RV_AA);
    rv_apply(RV_FPSHUD);
    fprintf(stderr, "[OPT] options: display %s, render scale %s, aspect %s, anisotropic filter %dx, antialiasing %s, "
            "fps counter %s, vsync %s, language %s\n",
            k_rv[RV_DISPLAY].val[rv_index(RV_DISPLAY)], saved ? settings_get(rv_render_key(ws), "") : "default",
            ws ? "16:9" : "4:3", nv2a_vk_aniso, k_rv[RV_AA].val[rv_index(RV_AA)],
            k_rv[RV_FPSHUD].val[rv_index(RV_FPSHUD)], k_rv[RV_VSYNC].val[rv_index(RV_VSYNC)],
            k_rv[RV_LANGUAGE].val[rv_index(RV_LANGUAGE)]);
}

/* ── CONTROLS > BUTTON MAPPING (porting guide M6; model THPS 2X bind_*, toolkit xinput_bind.h) ───────────────────
 * One player (Orta is single player): DEVICE (the keyboard, when it plays on port 0, or the controller on port 0;
 * left / right or A), RESET TO DEFAULT, then the device's bindings 6 per page and NEXT PAGE - the nine stock lines.
 * A on a binding records a press: every source released, then the first one pressed on that device (5 s without
 * one cancels); the game sees no input meanwhile (xbox_InputBindSuppress). Y resets the device. Duplicates are
 * marked DOUBLE. Bindings per device in saves\settings.ini (main.c xbox_InputBindSetStore, prefix ORTA_BIND). */
#include "xinput_bind.h"

#define BIND_PAGE 6
#define BIND_CAPTURE_FRAMES 300                    /* about 5 s of handler calls */
#define BIND_NEXT   0x100u
#define BIND_RESET  0x101u
#define BIND_DEVICE 0x103u
#define BIND_NONE   0x104u

static int s_bind_dev, s_bind_port = -3, s_bind_page;        /* port: -1 keyboard, -3 none */
static XboxBindMap s_bind_own, s_bind_def;
static uint32_t s_bind_line_ctl[9];
static char s_bind_text[9][48];
static int s_cap_control = -1, s_cap_phase, s_cap_frames;

static const char *const s_bind_ctl_label[XBOX_KEY_CONTROL_COUNT] = {
    "DPAD UP", "DPAD DOWN", "DPAD LEFT", "DPAD RIGHT", "START", "BACK", "L3", "R3",
    "A", "B", "X", "Y", "BLACK", "WHITE", "L TRIGGER", "R TRIGGER",
    "L STICK LEFT", "L STICK RIGHT", "L STICK DOWN", "L STICK UP",
    "R STICK LEFT", "R STICK RIGHT", "R STICK DOWN", "R STICK UP",
};

/* a pad name in the menu font (capitals, digits, '.', ':', space) */
static void bind_upper(char *dst, size_t n, const char *src)
{
    size_t i = 0;
    for (; *src && i + 1 < n; src++) {
        char c = *src;
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '.' || c == ':') dst[i++] = c;
        else if (i && dst[i - 1] != ' ') dst[i++] = ' ';
    }
    while (i && dst[i - 1] == ' ') i--;
    dst[i] = 0;
}

/* a source's name: a fixed text, or written into buf */
static const char *bind_src_label(uint16_t s, char *buf, size_t n)
{
    static const char *const pad[XBOX_PAD_SRC_COUNT] = {
        "NONE", "SOUTH", "EAST", "WEST", "NORTH", "BACK", "START", "L STICK", "R STICK",
        "L SHOULDER", "R SHOULDER", "DPAD UP", "DPAD DOWN", "DPAD LEFT", "DPAD RIGHT", "L TRIGGER", "R TRIGGER",
    };
    const char *t = NULL;
    if (!s) return "NONE";
    if (s_bind_port >= 0) return s < XBOX_PAD_SRC_COUNT ? pad[s] : "?";
    if (s >= 4 && s <= 29) { snprintf(buf, n, "%c", 'A' + (s - 4)); return buf; }
    if (s >= 30 && s <= 38) { snprintf(buf, n, "%c", '1' + (s - 30)); return buf; }
    if (s >= 58 && s <= 69) { snprintf(buf, n, "F%d", s - 57); return buf; }
    if (s >= 89 && s <= 97) { snprintf(buf, n, "KP %d", s - 88); return buf; }
    switch (s) {
    case 39: t = "0"; break;               case 40: t = "ENTER"; break;
    case 41: t = "ESCAPE"; break;          case 42: t = "BACKSPACE"; break;
    case 43: t = "TAB"; break;             case 44: t = "SPACE"; break;
    case 79: t = "RIGHT ARROW"; break;     case 80: t = "LEFT ARROW"; break;
    case 81: t = "DOWN ARROW"; break;      case 82: t = "UP ARROW"; break;
    case 88: t = "KP ENTER"; break;        case 98: t = "KP 0"; break;
    case 224: t = "L CTRL"; break;         case 225: t = "L SHIFT"; break;
    case 226: t = "L ALT"; break;          case 228: t = "R CTRL"; break;
    case 229: t = "R SHIFT"; break;        case 230: t = "R ALT"; break;
    }
    if (t) return t;
    snprintf(buf, n, "KEY %u", (unsigned)s);
    return buf;
}

/* the devices: the keyboard when it plays on port 0, the controller on port 0; returns the count */
static int bind_devices(int port[2])
{
    XboxBindMap o, d;
    char guid[33];
    int n = 0;
    if (xbox_InputKeyboardPort() == 0) port[n++] = -1;
    if (xbox_InputBindPad(0, guid, &o, &d)) port[n++] = 0;
    return n;
}

static void bind_select(void)
{
    int port[2], n = bind_devices(port);
    char guid[33];
    if (!n) { s_bind_port = -3; return; }
    s_bind_dev = ((s_bind_dev % n) + n) % n;
    s_bind_port = port[s_bind_dev];
    if (s_bind_port < 0) xbox_InputBindKeyboard(&s_bind_own, &s_bind_def);
    else if (!xbox_InputBindPad(s_bind_port, guid, &s_bind_own, &s_bind_def)) s_bind_port = -3;
}

static void bind_build(void)
{
    XboxBindKind kind = s_bind_port >= 0 ? XBOX_BIND_PAD : XBOX_BIND_KEYBOARD;
    uint32_t dup;
    int n, pages, c, line = 0;
    memset(s_bind_text, 0, sizeof s_bind_text);
    for (c = 0; c < 9; c++) s_bind_line_ctl[c] = BIND_NONE;
    if (s_bind_port == -3) { snprintf(s_bind_text[0], sizeof s_bind_text[0], "DEVICE: NONE CONNECTED"); return; }
    if (s_bind_port < 0) snprintf(s_bind_text[line], sizeof s_bind_text[line], "DEVICE: KEYBOARD");
    else {
        char name[64], up[24];
        xbox_InputPadName(s_bind_port, name, sizeof name);
        bind_upper(up, sizeof up, name);
        snprintf(s_bind_text[line], sizeof s_bind_text[line], "DEVICE: %s", up[0] ? up : "CONTROLLER");
    }
    s_bind_line_ctl[line++] = BIND_DEVICE;
    snprintf(s_bind_text[line], sizeof s_bind_text[line], "RESET TO DEFAULT");
    s_bind_line_ctl[line++] = BIND_RESET;
    dup = xbox_bind_duplicates(kind, &s_bind_own);
    n = xbox_bind_controls(kind);
    pages = (n + BIND_PAGE - 1) / BIND_PAGE;
    if (s_bind_page >= pages) s_bind_page = 0;
    for (c = s_bind_page * BIND_PAGE; c < n && c < (s_bind_page + 1) * BIND_PAGE; c++, line++) {
        char a[16], b[16], *t = s_bind_text[line];
        size_t sz = sizeof s_bind_text[line];
        s_bind_line_ctl[line] = (uint32_t)c;
        if (s_cap_control == c) {
            snprintf(t, sz, "%s: %s", s_bind_ctl_label[c], s_bind_port >= 0 ? "PRESS A BUTTON" : "PRESS A KEY");
            continue;
        }
        snprintf(t, sz, "%s: %s", s_bind_ctl_label[c], bind_src_label(s_bind_own.src[c][0], a, sizeof a));
        if (s_bind_own.src[c][1]) {                 /* a second source, when it fits (~34 glyphs a line) */
            const char *lb = bind_src_label(s_bind_own.src[c][1], b, sizeof b);
            size_t l = strlen(t);
            if (l + 1 + strlen(lb) <= 27) snprintf(t + l, sz - l, " %s", lb);
        }
        if (dup & (1u << c)) { size_t l = strlen(t); snprintf(t + l, sz - l, " DOUBLE"); }
    }
    snprintf(s_bind_text[8], sizeof s_bind_text[8], "NEXT PAGE  %d OF %d", s_bind_page + 1, pages);
    s_bind_line_ctl[8] = BIND_NEXT;
}

static void bind_store(void)
{
    if (s_bind_port == -1) xbox_InputBindStoreKeyboard(&s_bind_own);
    else if (s_bind_port >= 0) xbox_InputBindStorePad(s_bind_port, &s_bind_own);
}

static void bind_open(void)
{
    s_bind_dev = 0; s_bind_page = 0; s_cap_control = -1;
    bind_select();
    bind_build();
}

/* left / right on DEVICE: 1 when the screen changed */
static int bind_lr(uint32_t line, int dir)
{
    if (s_cap_control >= 0 || line > 8 || s_bind_line_ctl[line] != BIND_DEVICE) return 0;
    s_bind_dev += dir; s_bind_page = 0;
    bind_select(); bind_build();
    return 1;
}

/* Y: the device back to its default */
static int bind_reset(void)
{
    if (s_bind_port == -3 || s_cap_control >= 0) return 0;
    s_bind_own = s_bind_def;
    bind_store();
    fprintf(stderr, "[BIND] %s reset to default\n", s_bind_port >= 0 ? "pad" : "keyboard");
    bind_build();
    return 1;
}

/* A on a line: 1 when the screen changed */
static int bind_action(uint32_t line)
{
    uint32_t what = line < 9 ? s_bind_line_ctl[line] : BIND_NONE;
    if (s_cap_control >= 0 || what == BIND_NONE) return 0;
    if (what == BIND_DEVICE) return bind_lr(line, 1);
    if (what == BIND_RESET) return bind_reset();
    if (what == BIND_NEXT) { s_bind_page++; bind_build(); return 1; }
    s_cap_control = (int)what; s_cap_phase = 0; s_cap_frames = BIND_CAPTURE_FRAMES;
    xbox_InputBindSuppress(1);
    bind_build();
    return 1;
}

/* every handler call while a press is recorded; 1 = recording (the handler does nothing else), 2 = just done */
static int bind_tick(void)
{
    uint16_t held;
    if (s_cap_control < 0) return 0;
    held = s_bind_port >= 0 ? xbox_InputBindHeldPad(s_bind_port) : xbox_InputBindHeldKeyboard();
    if (s_cap_phase == 0) { if (!held) s_cap_phase = 1; }          /* the A that started it released first */
    else if (s_cap_phase == 1 && held) {
        char a[16];
        xbox_bind_set(&s_bind_own, s_cap_control, held);
        bind_store();
        fprintf(stderr, "[BIND] %s %s = %s\n", s_bind_port >= 0 ? "pad" : "keyboard",
                xbox_bind_control_name(s_cap_control),
                xbox_bind_source_name(s_bind_port >= 0 ? XBOX_BIND_PAD : XBOX_BIND_KEYBOARD, held, a, sizeof a));
        s_cap_phase = 2;
    } else if (s_cap_phase == 2 && !held) {                         /* released: the menu may see input again */
        s_cap_control = -1;
        xbox_InputBindSuppress(0);
        bind_build();
        return 2;
    }
    if (s_cap_phase < 2 && --s_cap_frames <= 0) {
        fprintf(stderr, "[BIND] no press in 5 s - unchanged\n");
        s_cap_control = -1;
        xbox_InputBindSuppress(0);
        bind_build();
        return 2;
    }
    return 1;
}

/* The text of a line in a view: the port views' lines are built from the values, the others are fixed. */
static const char *opt_view_text(int view, int line, char *buf, size_t n)
{
    if (view == OPT_V_BIND) {          /* never "": SetText hides a sprite with an empty string (flag 0x20) */
        snprintf(buf, n, "%s", line >= 0 && line < 9 && s_bind_text[line][0] ? s_bind_text[line] : " ");
        return buf;
    }
    if (view >= OPT_V_RECOMP) { rv_text(view, line, buf, n); return buf; }
    return k_opt_text[view][line];
}

/* ── View texts through the game's SetText ───────────────────────────────────────────────────────────────────────
 * sub_001B9AA0 (thiscall: ecx = sprite instance, one stack arg = NUL-terminated byte string, ret 4; 2026-11-04,
 * static): frees the old glyph slots, builds a new text node and one 2D-pool slot per glyph (pool ~512 glyphs for
 * everything on screen), bytes mapped through the font's char table (ASCII; the game passes plain literals from
 * 0x311684). Patching the record's text node instead cannot make a text longer than the stock one: the instance's
 * per-glyph slot array T+4 (T = inst+0x50, size T+0) is built at the stock length (crash run
 * 20261104_074634_opt_views2). Per record id a guest string; sub_001BB610 applies it at every create. */
extern void sub_001B9AA0(RECOMP_PARAMS);
extern void sub_001BB610_gen(RECOMP_PARAMS);
extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);
#define OPT_SETTEXT_MAX 48
static struct { uint32_t id, buf; int on; } s_settext[OPT_SETTEXT_MAX];
static uint32_t s_settext_n;
static uint32_t s_live_row[9], s_live_sel, s_live_sel_line = 99;   /* instances that got their SetText */
static uint32_t opt_settext_buf(uint32_t id);

/* a stock label (glyph codes) back to the ASCII SetText takes; 0 when a code has no known byte */
/* Through the game's own char table, inverted: SetText maps byte b to code [tbl + b*4 + 4], tbl = the first of
 * the tables at [[0x47E2D0]+0x10] (the default; sub_001B9AA0 picks by font id, the menu labels use the first).
 * The hand-made A-Z / space / . / - map missed 'Ä' (0x2E1), so "SE-LAUTSTÄRKE" kept BUTTON MAPPING's text when the
 * SOUND view came back (regress shot 09_sound, run 20261104_122823_regress_shots). */
static int opt_decode(uint32_t glyphs, uint32_t n, char *out, size_t cap)
{
    uint32_t ft = GUEST32(0x0047E2D0u), arr = ft ? GUEST32(ft + 0x10) : 0, tbl = arr ? GUEST32(arr) : 0, i, b;
    if (!glyphs || !tbl || n + 1 > cap) return 0;
    for (i = 0; i < n; i++) {
        uint32_t c = GUEST32(glyphs + 4 * i);
        for (b = 0x20; b < 0x100; b++)
            if (GUEST32(tbl + 4 + b * 4) == c) break;
        if (b == 0x100) return 0;
        out[i] = (char)b;
    }
    out[n] = 0;
    return 1;
}

/* The text sprite of a created instance. 2026-11-04: create returns a CONTAINER; its sprites are [inst+0x24]
 * and the +0x0C siblings, the text object T at sprite+0x50 (inst+0x50 is unrelated: SetText on it crashed, runs
 * 20261104_*_opt_views9..12). The game's own caller sub_000AC590 does the same ([[inst+0x24]+0x0C]). Taken: the
 * first sprite with a T whose drawn node is the record's own one or the one SetText built (T+0x5C == T+0x60). */
static uint32_t opt_text_sprite(uint32_t inst)
{
    /* 2026-11-04: a fresh sprite's T+0x5C (the drawn node) may not be attached yet - the draw copies T+0x60
     * into it - and SetText does not need it (it null-checks T+4 / +0x58 / +0x60). Requiring it skipped the labels
     * created on the way back from the video screen. Prefer a sprite with T+0x5C, else the first one with a T. */
    uint32_t s = inst ? GUEST32(inst + 0x24) : 0, n, first = 0;
    for (n = 0; s >= 0x00010000u && s < 0x04000000u && n < 16; s = GUEST32(s + 0x0C), n++) {
        uint32_t t = GUEST32(s + 0x50);
        if (t >= 0x00010000u && t < 0x04000000u) {
            if (GUEST32(t + 0x5C)) return s;
            if (!first) first = s;
        }
    }
    return first;
}

static void opt_settext(X86Ctx *ctx, uint8_t *base, uint32_t inst, uint32_t str)
{
    uint32_t spr = opt_text_sprite(inst);
    if (!spr) return;
    g_esp -= 4; GUEST32(g_esp) = str;
    g_esp -= 4; GUEST32(g_esp) = 0;        /* return address slot, popped by its ret 4 together with the arg */
    g_ecx = spr;
    sub_001B9AA0(ctx, base);
}

/* the string for record `id` (NULL: none, the record's own text again) */
static void opt_settext_set(uint32_t id, const char *t)
{
    uint32_t i, j, len;
    if (!id) return;
    for (i = 0; i < s_settext_n && s_settext[i].id != id; i++) ;
    if (i == s_settext_n) {
        if (!t || s_settext_n >= OPT_SETTEXT_MAX) return;
        if (!s_settext[i].buf) s_settext[i].buf = xbox_HeapAlloc(48, 16);   /* kept for the run, reused per id slot */
        if (!s_settext[i].buf) return;
        s_settext[i].id = id;
        s_settext_n++;
    }
    s_settext[i].on = t != NULL;
    if (!t) return;
    len = (uint32_t)strlen(t); if (len > 47) len = 47;
    for (j = 0; j < len; j++) *(volatile uint8_t *)((uintptr_t)g_xbox_mem_offset + s_settext[i].buf + j) = (uint8_t)t[j];
    *(volatile uint8_t *)((uintptr_t)g_xbox_mem_offset + s_settext[i].buf + len) = 0;
}

/* Set one line's label text (normal = plain row, selected): the SetText string for every later create, and for the
 * live instances at the next handler call (opt_settext_live). */
static void opt_line_text(int line, const char *t)
{
    uint32_t ref = GUEST32(0x0036BEDCu + 4 * (uint32_t)line);
    /* No record text node is patched any more (T+0x5C of every live instance points at it, so a longer count
     * overran their slot arrays - crash run 20261104_074634_opt_views2). Per instance only, through SetText. */
    /* SetText strings (normal = plain-row ids, selected); a line back to its stock text gets the stock text again
     * when a SetText of ours may still be on its instance (decoded; an undecodable label - umlaut - keeps ours) */
    {
        char stock[48];
        const char *s = t;
        if (!t) {
            uint32_t c = 0, g = 0, d, k, node, n, guard = 0, owner = GUEST32(0x0047E084u);
            for (n = owner ? GUEST32(owner + 8) : 0; n && guard < 4096; n = GUEST32(n + 0x18), guard++)
                if (GUEST32(n) == ref && (d = GUEST32(n + 4)) && (k = GUEST32(d + 0x54)) && (node = GUEST32(k))) {
                    opt_node_stock(node, &c, &g); break;
                }
            s = opt_settext_buf(ref) && opt_decode(g, c, stock, sizeof stock) ? stock : NULL;
        }
        opt_settext_set(ref, s);
        opt_settext_set(GUEST32(0x0036BF24u + 4 * (uint32_t)line), s);
    }
    s_live_row[line] = 0;                                      /* SetText again at the next handler call */
    if ((uint32_t)line == s_live_sel_line) s_live_sel = 0;
}

static void opt_layout(int view)
{
    int line, moved = 0;
    s_opt_view = view;
    s_opt_hidden = 0;
    for (line = 0; line < 9; line++) {                       /* 1: labels and values to their own parking rows */
        if (s_opt_row[line] < 40) { moved += opt_shift_line(line, s_opt_row[line], 40 + line, OPT_MV_LABEL); s_opt_row[line] = 40 + line; }
        if (s_opt_vrow[line] < 40) { moved += opt_shift_line(line, s_opt_vrow[line], 60 + line, OPT_MV_VALUE); s_opt_vrow[line] = 60 + line; }
    }
    for (line = 0; line < 9; line++) {                       /* 2: the view's lines to their rows, texts */
        int row = k_opt_rows[view][line];
        char buf[48];
        const char *t = opt_view_text(view, line, buf, sizeof buf);
        if (row < 0) { s_opt_hidden |= 1u << line; continue; }
        moved += opt_shift_line(line, s_opt_row[line], row, OPT_MV_LABEL);
        s_opt_row[line] = row;
        if (k_opt_values[view]) { moved += opt_shift_line(line, s_opt_vrow[line], row, OPT_MV_VALUE); s_opt_vrow[line] = row; }
        opt_line_text(line, t);
    }
    {
        static const char *const k_name[OPT_V_N] = { "TOP", "CONTROLS", "SOUND", "RECOMP", "RECOMP > ANTIALIASING",
                                                     "RECOMP > FRAME RATE", "RECOMP > BOOT ANIMATION",
                                                     "CONTROLS > BUTTON MAPPING", "RESTART NEEDED",
                                                     "RECOMP > BRIGHTNESS AND CONTRAST" };
        fprintf(stderr, "[OPT] view %s: %d position words moved\n", k_name[view], moved);
    }
}

/* All visible lines' texts of a view again (after a pick or a default changed more than one line) */
static void opt_view_texts(int view)
{
    int line;
    for (line = 0; line < 9; line++) {
        char buf[48];
        if (k_opt_rows[view][line] < 0) continue;
        opt_line_text(line, opt_view_text(view, line, buf, sizeof buf));
    }
}

/* After the sprite load (hook 0x0AF4BD): fresh records - reset the bookkeeping and lay out TOP. */
/* The data block of the first line's normal label when the layout was applied: another one = the records were
 * loaded again (the way back from the game's VIDEOEINSTELL. screen reloads them without the 0x0AF4BD hook - run
 * 20261104_*_opt_views20 came back to the stock nine lines), so the handler lays TOP out again. */
static uint32_t s_opt_layout_data;
static int s_opt_away;           /* the game's own video screen is open: no help texts of ours */
static uint32_t opt_label_data(void)
{
    uint32_t owner = GUEST32(0x0047E084u), n = owner ? GUEST32(owner + 8) : 0, guard = 0, id = GUEST32(0x0036BEDCu);
    for (; n && guard < 4096; n = GUEST32(n + 0x18), guard++)
        if (GUEST32(n) == id) return GUEST32(n + 4);
    return 0;
}

static void opt_apply_view(void)
{
    int line;
    s_opt_layout_data = opt_label_data();
    s_opt_away = 0;
    /* The SetText strings stay as they are: TOP's layout sets its visible lines (a line back to stock gets its
     * stock text again, opt_line_text) - switching all off here left the creates before the next handler call
     * with nothing (the way back from the video screen creates its labels before our handler runs). */
    for (line = 0; line < 9; line++) s_live_row[line] = 0;
    s_live_sel = 0; s_live_sel_line = 99;
    for (line = 0; line < 9; line++) s_opt_row[line] = s_opt_vrow[line] = line;
    opt_layout(OPT_V_TOP);
    s_opt_need_cursor = 1;
}

/* Run the generated handler once more with only `bits` in +0x18 (a return-address slot for its ret). */
static void opt_handler_again(X86Ctx *ctx, uint8_t *base, uint32_t obj, uint32_t bits)
{
    GUEST32(obj + 0x18) = bits;
    g_ecx = obj;
    g_esp -= 4;
    GUEST32(g_esp) = 0x000AF580u;
    sub_000AF580_gen(ctx, base);
}

/* Put the cursor on the first visible line of the current view, through the game's own "down" (sound, redraw,
 * help text follow): cursor = the line before it, then down, then the skip loop below. */
static void opt_cursor_first(X86Ctx *ctx, uint8_t *base, uint32_t obj)
{
    int line, first = -1, k;
    for (line = 0; line < 9 && first < 0; line++) if (!((s_opt_hidden >> line) & 1)) first = line;
    for (line = 0; line < 9; line++)                         /* the visible line in row 0 */
        if (!((s_opt_hidden >> line) & 1) && s_opt_row[line] == 0) first = line;
    if (first < 0) return;
    GUEST32(obj + 0x88) = (uint32_t)((first + 8) % 9);
    opt_handler_again(ctx, base, obj, 0x02000000u);
    for (k = 0; k < 9 && ((s_opt_hidden >> (GUEST32(obj + 0x88) % 9u)) & 1); k++)
        opt_handler_again(ctx, base, obj, 0x02000000u);
}

/* Sprite create sub_001BB610(record id) (cdecl; returns the instance, 0 when [0x47E2B8] == 0): a record id with a
 * view text gets it through the game's own SetText. 2026-11-04 (static): the options screen sub_000AE260 creates
 * its line sprites again at every selection change (selected label -> obj+0x6C, bar -> obj+0x74, normal labels not
 * stored), so a cached instance goes stale - this hook catches every create. */
void sub_001BB610(RECOMP_PARAMS)
{
    uint32_t id = GUEST32(g_esp + 4), inst, b;
    sub_001BB610_gen(ctx, base);
    inst = g_eax;
    if (inst && s_settext_n && (b = opt_settext_buf(id))) opt_settext(ctx, base, inst, b);
    g_eax = inst;
}

/* SetText on the options screen's stored label instances, once per new instance (called at the handler's start):
 * plain rows obj+0x3C + 4*line (sub_000AE470: the 0x36BF64 ids = the normal labels 0x36BEDC), the selected label
 * obj+0x6C (sub_000AE260, line = cursor). A line without a view text gets its stock text back (decoded). */
static uint32_t opt_settext_buf(uint32_t id)
{
    uint32_t i;
    for (i = 0; i < s_settext_n; i++) if (s_settext[i].id == id && s_settext[i].on) return s_settext[i].buf;
    return 0;
}
static void opt_settext_live(X86Ctx *ctx, uint8_t *base, uint32_t obj, uint32_t cur)
{
    int line;
    for (line = 0; line < 9; line++) {
        uint32_t inst = GUEST32(obj + 0x3C + 4 * (uint32_t)line), b;
        if (!inst || inst == s_live_row[line]) continue;
        s_live_row[line] = inst;
        if ((b = opt_settext_buf(GUEST32(0x0036BEDCu + 4 * (uint32_t)line)))) opt_settext(ctx, base, inst, b);
    }
    {
        uint32_t inst = GUEST32(obj + 0x6C), b;
        if (inst && (inst != s_live_sel || cur != s_live_sel_line)) {
            s_live_sel = inst; s_live_sel_line = cur;
            if ((b = opt_settext_buf(GUEST32(0x0036BF24u + 4 * cur)))) opt_settext(ctx, base, inst, b);
        }
    }
}

/* ── Help box texts for the port's lines ─────────────────────────────────────────────────────────────────────────
 * sub_000AE1F0(help id) (thiscall obj, ret 4): frees the old box [obj+0x14] (sub_001C0730, thiscall), looks the
 * message up (sub_001AFE00(id), ecx = [0x47DF20]; string = [[entry+0xC]+0xC]), builds the box with
 * sub_001C1D90(0, 0, str, 0, 0) and places it: sub_001C18C0(box, 1, 1, 0x140), sub_001C1920(box, 1, 0x17E). The
 * override does the same with its own string for lines whose stock help does not fit (RECOMP OPTIONS). */
extern void sub_000AE1F0_gen(RECOMP_PARAMS);
extern void sub_001C0730(RECOMP_PARAMS);
extern void sub_001C1D90(RECOMP_PARAMS);
extern void sub_001C18C0(RECOMP_PARAMS);
extern void sub_001C1920(RECOMP_PARAMS);
extern void sub_001AFE00(RECOMP_PARAMS);

static uint32_t opt_guest_call(X86Ctx *ctx, uint8_t *base, void (*fn)(RECOMP_PARAMS), uint32_t ecx, int n, ...)
{
    uint32_t a[8];
    va_list ap;
    int i;
    va_start(ap, n);
    for (i = 0; i < n && i < 8; i++) a[i] = va_arg(ap, uint32_t);
    va_end(ap);
    for (i = n - 1; i >= 0; i--) { g_esp -= 4; GUEST32(g_esp) = a[i]; }
    g_esp -= 4; GUEST32(g_esp) = 0;        /* return address slot (the callee's ret pops it) */
    g_ecx = ecx;
    fn(ctx, base);
    return g_eax;
}

/* Plain ASCII, '\n' between the box's lines (the stock strings: run 20261104_094159_opt_views16). NULL = stock. */
static const char *opt_help_for(int view, uint32_t line)
{
    static const char *const k_recomp[9] = {
        "Fullscreen, borderless or windowed.\nLeft or right to change, Y for the default.\nApplies at once.",
        "The game's 480 lines times this factor.\nLeft or right to change, Y for the default.\nApplies at once.",
        "4:3 as the original, or 16:9 widescreen.\nLeft or right to change, Y for the default.\nNeeds a restart.",
        "Sharper textures seen at an angle.\nLeft or right to change, Y for the default.\nApplies at once.",
        "Edge smoothing.\nPress A to open.",
        "Frame rate, VSync and FPS counter.\nPress A to open.",
        "Boot animation from media\\boot.mpg.\nPress A to open.",
        "Game language. SYSTEM follows Windows.\nLeft or right to change, Y for the default.\nNeeds a restart.",
        "The game's brightness and contrast.\nPress A to open.",
    };
    static const char *const k_fps[3] = {
        "The game's own 60 frames per second.\n120 comes in a later version.",
        "Waits for the display.\nLeft or right to change, Y for the default.\nNeeds a restart.",
        "Frames per second and frame time on screen.\nLeft or right to change, Y for the default.\nApplies at once.",
    };
    if (line > 8) return NULL;
    if (view == OPT_V_RECOMP) return k_recomp[line];
    if (view == OPT_V_AA)
        return "Smooths jagged edges.\nA to choose, Y for the default.\nApplies at once.";
    if (view == OPT_V_FPS && line < 3) return k_fps[line];
    if (view == OPT_V_BOOT)
        return "Plays media\\boot.mpg at every start.\nLeft or right to change, Y for the default.\nSee the README.";
    if (view == OPT_V_TOP && line == 2) return "Aiming, vibration and button mapping.\nPress A to open.";
    if (view == OPT_V_TOP && line == 7) return "Music, effects and voice volume.\nPress A to open.";
    if (view == OPT_V_TOP && line == 8) return "Display and graphics settings of the recomp.\nPress A to open.";
    if (view == OPT_V_CONTROLS && line == 0)   /* the game's help names the old label AUFW./ABW. */
        return "Up and down for aiming: normal or inverted.\nLeft or right to change, Y for the default.";
    if (view == OPT_V_CONTROLS && line == 2)
        return "Keys and buttons for the keyboard or the controller.\nPress A to open.";
    if (view == OPT_V_PIC)
        return line == 0 ? "Brightness of the picture.\nLeft or right to change, Y for the default.\nApplies at once."
                         : "Contrast of the picture.\nLeft or right to change, Y for the default.\nApplies at once.";
    if (view == OPT_V_RESTART)
        return line == 1 ? "Restarts the game with the new settings.\nUnsaved progress is lost."
             : line == 2 ? "The new settings apply at the next start." : "Some settings apply after a restart.";
    if (view == OPT_V_BIND) {
        uint32_t what = s_bind_line_ctl[line];
        if (what == BIND_DEVICE) return "Keyboard or controller.\nLeft or right to change.";
        if (what == BIND_RESET) return "This device back to its default keys.\nPress A or Y.";
        if (what == BIND_NEXT) return "The next bindings.\nPress A.";
        if (what < BIND_NEXT) return "Press A, then the new key or button.\nY resets the whole device.";
    }
    return NULL;
}

void sub_000AE1F0(RECOMP_PARAMS)
{
    uint32_t obj = g_ecx, box, str;
    static uint32_t gbuf;
    const char *t = opt_views_on() && !s_opt_away ? opt_help_for(s_opt_view, GUEST32(obj + 0x88) % 9u) : NULL;
    if (!t) { sub_000AE1F0_gen(ctx, base); return; }
    if (!gbuf) gbuf = xbox_HeapAlloc(256, 16);
    if (!gbuf) { sub_000AE1F0_gen(ctx, base); return; }
    {
        uint32_t j, len = (uint32_t)strlen(t);
        if (len > 255) len = 255;
        for (j = 0; j < len; j++) *(volatile uint8_t *)((uintptr_t)g_xbox_mem_offset + gbuf + j) = (uint8_t)t[j];
        *(volatile uint8_t *)((uintptr_t)g_xbox_mem_offset + gbuf + len) = 0;
    }
    g_esp += 8;                                                /* our own ret 4: return address + the id */
    if ((box = GUEST32(obj + 0x14))) { opt_guest_call(ctx, base, sub_001C0730, box, 0); GUEST32(obj + 0x14) = 0; }
    str = gbuf;
    box = opt_guest_call(ctx, base, sub_001C1D90, 0, 5, 0u, 0u, str, 0u, 0u); g_esp += 20;
    GUEST32(obj + 0x14) = box;
    if (box) {
        opt_guest_call(ctx, base, sub_001C18C0, 0, 4, box, 1u, 1u, 0x140u); g_esp += 16;
        opt_guest_call(ctx, base, sub_001C1920, 0, 3, box, 1u, 0x17Eu); g_esp += 12;
    }
}

void sub_000AF580(RECOMP_PARAMS)
{
    uint32_t obj = g_ecx, bits = obj ? GUEST32(obj + 0x18) : 0, cur;
    uint32_t move = bits & (0x01000000u | 0x02000000u);
    int k;
    if (!obj || !opt_views_on()) { sub_000AF580_gen(ctx, base); return; }
    {
        uint32_t d = opt_label_data();
        /* records loaded again, or back from the game's video screen (the list handler does not run there) */
        if ((d && d != s_opt_layout_data) || s_opt_away) {
            /* Known: line 7 keeps "VIDEOEINSTELL." after the way back (run 20261104_*_opt_views21). Moving the
             * cursor over it once more made line 8 stale too (run 20261104_*_opt_views24) - reverted, open. */
            fprintf(stderr, "[OPT] options records reloaded (0x%08X -> 0x%08X): layout again\n", s_opt_layout_data, d);
            opt_apply_view();
        }
    }
    if (s_opt_need_cursor) {                                  /* the stock cursor starts on line 0, parked in TOP */
        s_opt_need_cursor = 0;
        opt_cursor_first(ctx, base, obj);
        GUEST32(obj + 0x18) = bits;
    }
    cur = GUEST32(obj + 0x88) % 9u;
    opt_settext_live(ctx, base, obj, cur);                    /* view texts onto the finished label instances */
    if (s_opt_view == OPT_V_BIND && s_cap_control >= 0) {     /* BUTTON MAPPING records a press: nothing else */
        int r = bind_tick();
        if (r == 2) opt_view_texts(OPT_V_BIND);
        GUEST32(obj + 0x18) = 0;
        g_ecx = obj;
        sub_000AF580_gen(ctx, base);
        GUEST32(obj + 0x18) = bits;
        return;
    }
    if ((bits & 0x8u) && s_opt_view == OPT_V_BIND && bind_reset()) opt_view_texts(OPT_V_BIND);   /* Y: reset */
    if ((bits & 0x8u) && (s_opt_view == OPT_V_CONTROLS || s_opt_view == OPT_V_SOUND)) {
        /* Y = the game's default of this line (maintainer: Y resets everywhere), through the game's own left /
         * right so its sprites, sound and saving follow: the start values in the XBE's data (0x34274C AUF/AB 0 =
         * NORMAL, 0x342750 VIBRATION 1 = EIN, 0x34276C.. the three volumes 1.0f = full). Volumes: right until the
         * top (it stops there); AIMING / VIBRATION: one switch (left / right XOR 1) when not at the default. */
        uint32_t v = GUEST32(obj + 0x8C + 4 * cur);
        int n = 0;
        if (s_opt_view == OPT_V_SOUND && cur >= 3 && cur <= 5) n = 10;
        if (s_opt_view == OPT_V_CONTROLS && ((cur == 0 && v != 0) || (cur == 1 && v != 1))) n = 1;
        for (k = 0; k < n; k++) opt_handler_again(ctx, base, obj, 0x08000000u);
        if (n) fprintf(stderr, "[OPT] view %d line %u: Y -> default (%d x right, value %u -> %u)\n", s_opt_view, cur,
                       n, v, GUEST32(obj + 0x8C + 4 * cur));
        GUEST32(obj + 0x18) = bits & ~0x8u;
        g_ecx = obj;
        sub_000AF580_gen(ctx, base);
        GUEST32(obj + 0x18) = bits;
        return;
    }
    if ((bits & 0x8u) && s_opt_view >= OPT_V_RECOMP) {        /* Y (0x00000008, run _094159_opt_views16): default */
        if (rv_default(s_opt_view, (int)cur)) {
            opt_view_texts(s_opt_view);
            fprintf(stderr, "[OPT] view %d line %u: default\n", s_opt_view, cur);
        }
    }
    if (bits & 0x101u) {                                     /* A */
        int to = -1, swallow;
        const opt_pl_t *pl = opt_pl(s_opt_view, (int)cur);
        if (s_opt_view == OPT_V_TOP && cur == 2) to = OPT_V_CONTROLS;
        if (s_opt_view == OPT_V_TOP && cur == 7) to = OPT_V_SOUND;
        if (s_opt_view == OPT_V_TOP && cur == 8) to = OPT_V_RECOMP;
        if (s_opt_view == OPT_V_CONTROLS && cur == 2) { to = OPT_V_BIND; bind_open(); }
        if (pl && pl->kind == PL_SUB) to = pl->a;
        if (pl && pl->kind == PL_TEXT && pl->a == 3) {          /* RESTART NEEDED > RESTART NOW */
            extern void host_restart(void);                   /* platform/host.h: same arguments, start env */
            fprintf(stderr, "[OPT] RESTART NOW\n");
            host_restart();                                   /* returns only if the new process failed */
            fprintf(stderr, "[OPT] restart failed - the values apply at the next start\n");
        }
        if (pl && pl->kind == PL_TEXT && (pl->a == 3 || pl->a == 4)) {   /* LATER (or a failed restart): to TOP */
            bits &= ~0x101u;                                  /* the B code below runs the game's handler once */
            goto do_back;
        }
        swallow = to >= 0 || (s_opt_view == OPT_V_CONTROLS && cur == 2) || s_opt_view >= OPT_V_RECOMP;
        if (swallow) {
            GUEST32(obj + 0x18) = bits & ~0x101u;              /* the game's A on these lines is not wanted */
            g_ecx = obj;
            sub_000AF580_gen(ctx, base);
            if (to >= 0) { opt_layout(to); opt_cursor_first(ctx, base, obj); }
            else if (s_opt_view == OPT_V_BIND) { if (bind_action(cur)) opt_view_texts(OPT_V_BIND); }
            else if (rv_pick(s_opt_view, (int)cur)) {
                opt_view_texts(s_opt_view);
                fprintf(stderr, "[OPT] view %d line %u: picked\n", s_opt_view, cur);
            }
            GUEST32(obj + 0x18) = bits;
            return;
        }
    }
    if ((bits & (0x08000000u | 0x04000000u)) && s_opt_view >= OPT_V_RECOMP) {   /* left / right: our values */
        /* 0x04000000 = LEFT, 0x08000000 = RIGHT (run 20261104_090544_opt_views14: a scripted left arrived as
         * 0x44004000; down = 0x22002000, B = 0x00000002) */
        int dir = (bits & 0x08000000u) ? 1 : -1;
        GUEST32(obj + 0x18) = bits & ~(0x08000000u | 0x04000000u);   /* not the stock line's own value */
        g_ecx = obj;
        sub_000AF580_gen(ctx, base);
        if (s_opt_view == OPT_V_BIND) { if (bind_lr(cur, dir)) opt_view_texts(OPT_V_BIND); }
        else if (rv_step(s_opt_view, (int)cur, dir)) {
            char buf[48];
            rv_text(s_opt_view, (int)cur, buf, sizeof buf);
            opt_line_text((int)cur, buf);                     /* SetText on its instances at the next call */
            fprintf(stderr, "[OPT] view %d line %u -> %s\n", s_opt_view, cur, buf);
        }
        GUEST32(obj + 0x18) = bits;
        return;
    }
    if ((bits & 0x202u) && s_opt_view != OPT_V_TOP) {         /* B in a sub-view: back to its parent */
        int back_line, parent;
    do_back:
        back_line = s_opt_view == OPT_V_CONTROLS ? 2 : s_opt_view == OPT_V_SOUND ? 7 : s_opt_view == OPT_V_BIND ? 2 : 8;
        parent = OPT_V_TOP;
        if (s_opt_view == OPT_V_BIND) parent = OPT_V_CONTROLS;  /* back_line 2 = BUTTON MAPPING (set above) */
        else if (s_opt_view == OPT_V_RECOMP && rv_restart_pending()) { parent = OPT_V_RESTART; back_line = 1; }
        else if (s_opt_view == OPT_V_RESTART) parent = OPT_V_TOP;   /* LATER / B: TOP on RECOMP OPTIONS */
        else if (s_opt_view > OPT_V_RECOMP) {                  /* a RECOMP sub-list: back to its RECOMP line */
            int l;
            parent = OPT_V_RECOMP;
            for (l = 0; l < 9; l++)
                if (k_pl[OPT_V_RECOMP][l].kind == PL_SUB && k_pl[OPT_V_RECOMP][l].a == s_opt_view) back_line = l;
        }
        GUEST32(obj + 0x18) = bits & ~0x202u;
        g_ecx = obj;
        sub_000AF580_gen(ctx, base);
        opt_layout(parent);
        GUEST32(obj + 0x88) = (uint32_t)((back_line + 8) % 9);  /* land on the line the sub-view came from */
        opt_handler_again(ctx, base, obj, 0x02000000u);
        for (k = 0; k < 9 && ((s_opt_hidden >> (GUEST32(obj + 0x88) % 9u)) & 1); k++)
            opt_handler_again(ctx, base, obj, 0x02000000u);
        GUEST32(obj + 0x18) = bits;
        return;
    }
    if (move) {
        /* Up / down by SCREEN ROW, not by line index (run 20261104_072323_opt_views1: down from CONTROLS, line 2,
         * went to line 6 = ANZEIGE instead of SOUND, line 7, one row below). Target = the visible line in the next
         * / previous row (wrapping); the cursor is set to the line before it and the game's own "down" moves it
         * there (sound, redraw, help text as stock). The rest of this frame's bits run as usual. */
        int rows = 0, row = s_opt_row[cur], to_row, target = -1, line;
        for (line = 0; line < 9; line++) if (!((s_opt_hidden >> line) & 1)) rows++;
        if (rows > 0 && row < 40) {
            to_row = (bits & 0x02000000u) ? (row + 1) % rows : (row + rows - 1) % rows;
            for (line = 0; line < 9; line++) if (!((s_opt_hidden >> line) & 1) && s_opt_row[line] == to_row) target = line;
        }
        GUEST32(obj + 0x18) = bits & ~(0x01000000u | 0x02000000u);
        g_ecx = obj;
        sub_000AF580_gen(ctx, base);
        if (target >= 0) {
            GUEST32(obj + 0x88) = (uint32_t)((target + 8) % 9);
            opt_handler_again(ctx, base, obj, 0x02000000u);
        }
        GUEST32(obj + 0x18) = bits;
        return;
    }
    g_ecx = obj;   /* opt_cursor_first's extra handler runs leave ecx changed (run 20261104_074744_opt_views1: ecx 0) */
    sub_000AF580_gen(ctx, base);
}

/* ── Options: new label records = clones of a stock value-less label (step 2 of the build plan) ─────────────────
 *
 * Shape of a plain label record (from opt_snap 20261103_233130, VERLASSEN 0x143 / 0x145; recgraph_143.txt):
 *   data D (0x94): +0x54 / +0x58 / +0x60 -> 16-slot arrays (0x40, per record, slot[0] points back into D), +0x64 ->
 *   colour / animation keys K (0x78), +0x8C glyph count, +0x90 -> glyph block G (glyphs, a 0 word, then text
 *   parameters reached through the +0x60 slot; 0x4C for 9 glyphs); K+0x6C -> position key P (0x0C; +4 = x | y<<16).
 * A clone copies those seven blocks into port-owned guest heap (xbox_HeapAlloc, kept for the whole run - the game
 * never frees it because the node is linked at the END of the owner list, outside every group's node span that
 * sub_001B7330 unloads), relocates every word that points into a copied block, gives the text its own glyph array
 * (n codes + a 0 word) and the row its own y. Node: 0x1C bytes {id, data, 0, 0, 0, prev, next}. The loaded set is
 * reloaded on every open, so the clones are rebuilt from the fresh template each time (orta_opt_after_load). */
extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);

typedef struct { uint32_t old_va, size, new_va; } opt_blk;

static uint32_t opt_find_data(uint32_t id, uint32_t *node_out)
{
    uint32_t owner = GUEST32(0x0047E084u), n = owner ? GUEST32(owner + 8) : 0, guard = 0;
    for (; n && guard < 4096; n = GUEST32(n + 0x18), guard++)
        if (GUEST32(n) == id) { if (node_out) *node_out = n; return GUEST32(n + 4); }
    return 0;
}

/* Returns 1 when `new_id` exists afterwards (built now or already linked). */
static int opt_clone_label(uint32_t tmpl_id, uint32_t new_id, const char *text, uint32_t x, uint32_t y)
{
    static uint32_t mem[64][2];   /* per clone slot: id, heap block (allocated once, reused on every open) */
    uint32_t d, k, g, p, owner, n, last, guard = 0, heap = 0, off, i, j, nb, len, ngl;
    opt_blk b[7];
    int slot = -1, si;
    if (opt_find_data(new_id, NULL)) return 1;
    if (!(d = opt_find_data(tmpl_id, NULL))) { fprintf(stderr, "[OPT] clone: template 0x%X not loaded\n", tmpl_id); return 0; }
    k = GUEST32(d + 0x64); g = GUEST32(d + 0x90); p = k ? GUEST32(k + 0x6C) : 0;
    if (!k || !g || !p || !GUEST32(d + 0x54) || !GUEST32(d + 0x58) || !GUEST32(d + 0x60)) {
        fprintf(stderr, "[OPT] clone: template 0x%X data 0x%08X unexpected shape (k %08X g %08X p %08X s %08X %08X %08X)\n",
                tmpl_id, d, k, g, p, GUEST32(d + 0x54), GUEST32(d + 0x58), GUEST32(d + 0x60));
        return 0;
    }
    len = (uint32_t)strlen(text);
    if (!len || len > 32) return 0;
    b[0] = (opt_blk){ d, 0x94, 0 };            b[1] = (opt_blk){ k, 0x78, 0 };
    b[2] = (opt_blk){ g, 0x4C, 0 };            b[3] = (opt_blk){ p, 0x0C, 0 };
    b[4] = (opt_blk){ GUEST32(d + 0x54), 0x40, 0 };
    b[5] = (opt_blk){ GUEST32(d + 0x58), 0x40, 0 };
    b[6] = (opt_blk){ GUEST32(d + 0x60), 0x40, 0 };
    nb = 7;
    /* one heap block per clone id: node + 7 blocks + glyph array, 16-aligned pieces */
    for (si = 0; si < 64; si++) if (mem[si][0] == new_id || !mem[si][0]) { slot = si; break; }
    if (slot < 0) return 0;
    if (!mem[slot][0]) {
        mem[slot][0] = new_id;
        mem[slot][1] = xbox_HeapAlloc(0x20 + 0x94 + 0x78 + 0x50 + 0x10 + 3 * 0x40 + 0x90 + 0x40, 16);
    }
    if (!(heap = mem[slot][1])) { fprintf(stderr, "[OPT] clone: xbox_HeapAlloc failed\n"); return 0; }
    off = heap + 0x20;                          /* node at heap+0 */
    for (i = 0; i < nb; i++) { b[i].new_va = off; off += (b[i].size + 15u) & ~15u; }
    ngl = off;                                  /* the new glyph array */
    for (i = 0; i < nb; i++)
        for (j = 0; j < b[i].size; j += 4) {
            uint32_t v = GUEST32(b[i].old_va + j), m;
            for (m = 0; m < nb; m++)
                if (v >= b[m].old_va && v < b[m].old_va + b[m].size) { v = b[m].new_va + (v - b[m].old_va); break; }
            GUEST32(b[i].new_va + j) = v;
        }
    for (i = 0; i < len; i++) GUEST32(ngl + 4 * i) = opt_glyph(text[i]);
    GUEST32(ngl + 4 * len) = 0;
    GUEST32(b[0].new_va + 0x8C) = len;
    GUEST32(b[0].new_va + 0x90) = ngl;
    GUEST32(b[3].new_va + 4) = (x & 0xFFFFu) | (y << 16);
    /* node, linked after the last node of the owner list */
    owner = GUEST32(0x0047E084u);
    for (last = n = GUEST32(owner + 8); n && guard < 4096; n = GUEST32(n + 0x18), guard++) last = n;
    if (!last) { fprintf(stderr, "[OPT] clone: owner list empty\n"); return 0; }
    GUEST32(heap + 0x00) = new_id; GUEST32(heap + 0x04) = b[0].new_va;
    GUEST32(heap + 0x08) = 0; GUEST32(heap + 0x0C) = 0; GUEST32(heap + 0x10) = 0;
    GUEST32(heap + 0x14) = last; GUEST32(heap + 0x18) = 0;
    GUEST32(last + 0x18) = heap;
    return 1;
}
