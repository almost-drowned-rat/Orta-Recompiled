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
#define GUEST8(va) (*(volatile uint8_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))

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
static int orta_launch_saturn_bonus(void);
static void orta_saturn_text(void);

/* sub_000121C0 = the Saturn bonus game's launcher (Pandora's Box; the only caller of XLaunchNewImage with
 * "d:\pd1.xbe"): it tears the game down (sub_00033A70 / 00014FA0 / 00012060 / 0003F990 / 00013B70: sound, streams,
 * system data save) and calls D3DDevice_PersistDisplay (sub_002ABB20) before the launch. Known issue #4 (2026-11-10,
 * after the first play-through): selecting the entry HUNG in PersistDisplay - it waits for D3D's flip queue to
 * drain, and the queue's flip targets had run ~2.4 M vblanks ahead of D3D's own vblank count over a long session
 * (read from a dump of the hung process; root cause in the toolkit not traced yet, toolkit docs/lessons_learned.md). The launch is not planned anyway, so the whole function is replaced: the notice, then back to the
 * menu - nothing is torn down, the menu keeps its sound and the game goes on. A later Saturn recomp would be
 * started from orta_launch_saturn_bonus(). cdecl, no parameters, the return value is unused (it never returns on
 * the console). */
/* JAPANESE (GitHub #2, 2026-11-10). The
 * PAL disc carries every Japanese text file (menudata\TextData\*_JP.msg, MessageEvent\MesData_*_JP.msg), but the
 * PAL startup sub_00011820 maps XGetLanguage() with a switch on (lang - 3): German 2, French 3, Spanish 4, Italian
 * 5, everything else (English 1 AND Japanese 2) -> 1, and hands that to sub_0002A970 = "set the game language"
 * (stores it in 0x342760; read back by sub_0002A980 for the file names and the 6-case text switches). Index 0 is
 * Japanese there. With LANGUAGE: JAPANESE (main.c -> XC_LANGUAGE 2) the setter gets 0 instead. */
#include "xbox_settings.h"   /* toolkit: xbox_settings()->language */
extern void sub_0002A970_gen(RECOMP_PARAMS);
void sub_0002A970(RECOMP_PARAMS)
{
    static int logged;
    orta_saturn_text();   /* once, at the startup's language set (the dialog tables are static data) */
    if (xbox_settings()->language == XBOX_LANG_JAPANESE && GUEST32(g_esp + 4) == 1u) {
        GUEST32(g_esp + 4) = 0u;
        if (!logged++) { fprintf(stderr, "[ORTA] language: Japanese -> game language index 0\n"); fflush(stderr); }
    }
    sub_0002A970_gen(ctx, base);
}

extern void sub_000121C0_gen(RECOMP_PARAMS);
void sub_000121C0(RECOMP_PARAMS)
{
    (void)base;
    fprintf(stderr, "[LAUNCH] Saturn bonus game selected (sub_000121C0) - not planned, back to the menu\n");
    fflush(stderr);
    if (!orta_launch_saturn_bonus()) {
        g_eax = 0;
        g_esp += 4;                          /* ret */
        return;
    }
    sub_000121C0_gen(ctx, base);
}
#define ORTA_ERROR_NOT_SUPPORTED 50u

/* The Sega Saturn "Panzer Dragoon" in Pandora's Box. Returns 1 if it ran (never, for now). The player is told in
 * the game's own question box (orta_saturn_text below), so nothing is shown here (a Windows notice
 * tried first went behind the game window). */
static int orta_launch_saturn_bonus(void)
{
    return 0;
}

/* The game asks "Panzer Dragoon anfangen zu spielen?  Ja / Nein" with message 0x14 of its dialog tables (the six
 * language tables of pm_dialog_text below; found 2026-11-10 by reading the tables: German entry 0x14 =
 * "Panzer Dragoon anfangen zu spielen?\n\n%s0Ja%s0         %s1Nein%s1"). Message 0x14 is used by nothing else, so
 * its text is replaced for good, in every language, by an English notice with one
 * answer, OK. The box draws "%s0..%s0" / "%s1..%s1" as its answers. OK = answer 0 = the stock's "yes" -> the
 * launcher sub_000121C0 (replaced above) returns to the menu at once. */
static void orta_saturn_text(void)
{
    static int done;
    /* no yes / no question: the notice at once, with OK */
    static const char k_txt[] = "Panzer Dragoon, the Sega Saturn original,\nis not part of this recomp and cannot be\n"
                                "started here. Sorry about that - and thank\nyou for playing Panzer Dragoon Orta!\n\n%s0OK%s0";
    uint32_t ent[6], k, j, mine;
    const uint32_t id = 0x14u;
    if (done) return;
    done = 1;
    ent[0] = 0x003D5E98u + 12u * id; ent[1] = 0x003D5FB8u + 12u * id; ent[2] = 0x003D60D8u + 12u * id;
    ent[3] = 0x003D61F8u + 12u * id; ent[4] = 0x003D5C58u + 12u * (4u + 2u * id); ent[5] = 0x003D5C64u + 24u * id;
    mine = xbox_HeapAlloc(sizeof k_txt + 16u, 16);
    if (!mine) { fprintf(stderr, "[LAUNCH] Saturn notice text: no memory - the stock question stays\n"); return; }
    for (j = 0; j < sizeof k_txt; j++)
        *(volatile uint8_t *)((uintptr_t)g_xbox_mem_offset + mine + j) = (uint8_t)k_txt[j];
    for (k = 0; k < 6; k++) {
        uint32_t old = GUEST32(ent[k] + 8);
        if (old >= 0x00300000u && old < 0x00400000u) GUEST32(ent[k] + 8) = mine;
    }
    fprintf(stderr, "[LAUNCH] Saturn question (message 0x14) -> \"not planned\" notice with OK\n");
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

/* A font's char table (sprite\Font\*_h_tbl.bin as loaded: header 0, 1, 0x1C, 0x1C, 0x200, then u16 glyph indices
 * per byte). Two neighbouring indices read as one "x | y << 16" word inside a row band - known issue #18: the walk
 * reached the help box's Arial table and moved the odd codes 0x55..0x75 by 150 (5 rows), so English help texts lost
 * a c e g i k m o q s u U W Y (runs 20261111_075533_help_m6_en: 'a' 65 -> 215 on entering RECOMP OPTIONS; B's vertex
 * dumps 20261111_074246_issue18_b_boxverts_h05: the glyph quads pointed at empty atlas cells). Never walked. */
static int opt_is_font_table(uint32_t p)
{
    return GUEST32(p) == 0 && GUEST32(p + 4) == 1 && GUEST32(p + 8) == 0x1C && GUEST32(p + 12) == 0x1C &&
           GUEST32(p + 16) == 0x200u;
}

static int opt_shift_walk(uint32_t p, int depth, uint32_t old_y, int dy)
{
    int i, n = 0;
    if (depth > 3 || p < 0x00400000u || p >= 0x04000000u || !opt_set_add(0, p, depth)) return 0;
    if (opt_is_font_table(p)) return 0;
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
       OPT_V_RESTART, OPT_V_PIC, OPT_V_VIEW, OPT_V_N };
static const signed char k_opt_rows[OPT_V_N][9] = {
    /* line:   0   1   2   3   4   5   6   7   8 */
    /* TOP: PRACTICE (line 0, its AIMING value parked; maintainer 2026-11-04 late: ÜBUNG moves here as the first line),
     * ANZEIGE, SOUND, CONTROLS, RECOMP (maintainer 2026-11-04 ~08:00) */
    {          0, -1,  3, -1, -1, -1,  1,  2,  4 },
    {          0,  1,  2, -1, -1, -1, -1, -1, -1 },   /* CONTROLS */
    {         -1, -1, -1,  0,  1,  2,  3, -1, -1 },   /* SOUND: BGM / SE / STIMMEN, SPEAKERS (line 6, port value) */
#ifdef __ANDROID__
    /* Android: no DISPLAY MODE (the app is always full screen, the line has no function there) - line 1 hidden,
     * the others move up a row */
    {          0, -1,  1,  2,  3,  4,  5,  6,  7 },   /* RECOMP OPTIONS without DISPLAY MODE */
#else
    {          0,  1,  2,  3,  4,  5,  6,  7,  8 },   /* RECOMP OPTIONS: every stock line, value sprites parked */
#endif
    {          0,  1,  2,  3,  4, -1, -1, -1, -1 },   /* RECOMP > ANTIALIASING: the five choices */
    {          0,  1,  2,  3, -1, -1, -1, -1, -1 },   /* RECOMP > FRAME RATE: FRAME RATE, FRAME PACING, VSYNC, FPS COUNTER */
    {          0,  1, -1, -1, -1, -1, -1, -1, -1 },   /* RECOMP > BOOT ANIMATION: status, ON / OFF */
    {          0,  1,  2,  3,  4,  5,  6,  7,  8 },   /* CONTROLS > BUTTON MAPPING: DEVICE, RESET, 6 bindings, NEXT */
    {          0,  1,  2, -1, -1, -1, -1, -1, -1 },   /* RESTART NEEDED: the line, RESTART NOW, LATER */
    {          0,  1, -1, -1, -1, -1, -1, -1, -1 },   /* RECOMP > BRIGHTNESS AND CONTRAST: the game's two values */
    {          0,  1, -1, -1, -1, -1, -1, -1, -1 },   /* RECOMP > ASPECT RATIO AND FOV: ASPECT RATIO, FIELD OF VIEW */
};
/* views whose lines show their stock value sprites (others park them, "NAME: VALUE" is in the text) */
static const unsigned char k_opt_values[OPT_V_N] = { 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 };
static const char *const k_opt_text[OPT_V_N][9] = {
    { "PRACTICE", 0, "CONTROLS", 0, 0, 0, 0, "SOUND", "RECOMP OPTIONS" },
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
 * The view's stock lines 0..8 carry (order: maintainer 2026-11-05): BRIGHTNESS AND CONTRAST >, DISPLAY MODE, RENDER
 * RESOLUTION, ASPECT RATIO, ANISOTROPIC FILTER, ANTIALIASING >, FRAME RATE >, LANGUAGE, BOOT ANIMATION >.
 * Left / right step a value line, save it at once (saves\settings.ini) and apply it where the toolkit can at run
 * time (display mode, render scale, anisotropic filter); ASPECT RATIO and LANGUAGE apply at the next start (main.c
 * reads them before the device / XAPI settings exist). The sub-lists (">") and BRIGHTNESS are not built yet. */
#include "settings.h"
#include "nv2a_backend.h"   /* toolkit: xbox_video_aspect (21:9 = the display's exact ratio) */
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

typedef struct { const char *label, *key, *def; const char *val[8], *txt[8]; int n; } opt_val_t;
enum { RV_DISPLAY, RV_RENDER, RV_ASPECT, RV_ANISO, RV_LANGUAGE, RV_AA, RV_FRAMERATE, RV_VSYNC, RV_FPSHUD, RV_BOOT,
       RV_PACING, RV_FOV, RV_SPEAKERS, RV_N };
static const opt_val_t k_rv[RV_N] = {
    /* index = NV2A_VK_AA_*; applied at once if the device can (nv2a_vk_antialiasing_fits), else refused */
    [RV_AA]       = { "ANTIALIASING", "antialiasing", "fxaa", { "off", "fxaa", "smaa", "msaa2", "msaa4" },
                      { "OFF", "FXAA", "SMAA", "MSAA 2X", "MSAA 4X" }, 5 },
    /* 120 is M7 (after M6, maintainer 2026-11-04): ORIGINAL only until then */
    /* 120 (night order 2026-11-07, item 7): guest vblank 120 Hz + the game's own frame time step 1/120 (orta_fps_*
     * below); at start (main.c RECOMP_GUEST_HZ) - RESTART NEEDED */
    /* 120 NOT offered yet (2026-11-08 night test, runs 20261108_011846_fps60 / _012117_fps120): with dt 1/120 the
     * flight path and the other per-frame steps still advance once per game frame - at 120 the level ran at twice
     * the speed (game frame 1200 = the same scene, after 10 s instead of 20 s). docs/fps120_study.md lists the
     * per-frame steps to halve. ORTA_FPS=120 still forces it for tests. */
    [RV_FRAMERATE] = { "FRAME RATE", "frame_rate", "original", { "original", "120" }, { "ORIGINAL 60", "120" }, 1 },
    [RV_VSYNC]    = { "VSYNC", "vsync", "on", { "on", "off" }, { "ON", "OFF" }, 2 },      /* main.c, at start */
    /* FRAME PACING (port guideline, maintainer 2026-11-05; THPS 2X v1.1.0): DISPLAY = on a 120 / 180 / 240 Hz display
     * the guest vblank follows the display's own vblank (toolkit host_vblank, option-c night 2026-11-07), 60 HZ
     * TIMER = the old clock. main.c -> RECOMP_FRAME_PACING, at start. */
    [RV_PACING]   = { "FRAME PACING", "frame_pacing", "display", { "display", "timer" }, { "DISPLAY", "TIMER" }, 2 },
    /* "TIMER", not THPS's "60 HZ TIMER": "FRAME PACING: 60 HZ TIMER - RESTART" ran off the screen when selected
     * (run 20261107_213826_pacing_menu shot 02); the help line names the 60 Hz timer */
    [RV_FPSHUD]   = { "FPS COUNTER", "fps_counter", "off", { "off", "fps", "fps_frametime", "frametime" },
                      { "OFF", "FPS", "FPS AND FRAME TIME", "FRAME TIME" }, 4 },        /* = NV2A_VK_HUD_* */
    [RV_BOOT]     = { "BOOT ANIMATION", "boot_animation", "on", { "on", "off" }, { "ON", "OFF" }, 2 },
    [RV_DISPLAY]  = { "DISPLAY MODE", "display_mode", "borderless", { "fullscreen", "borderless", "windowed" },
                      { "FULLSCREEN", "BORDERLESS", "WINDOWED" }, 3 },
    [RV_RENDER]   = { "RENDER RESOLUTION", NULL, NULL, { 0 }, { 0 }, 0 },          /* opt_render_* */
    /* 16:10 / 21:9 (night order 2026-11-07; THPS 2X v1.2.0): Orta's own 16:9 path, widened further by the port
     * (orta_aspect_* below); 21:9 = the display's exact ratio in the 21:9 range, else 64:27 (orta_aspect_21x9) */
    [RV_ASPECT]   = { "ASPECT RATIO", "aspect_ratio", "4:3", { "4:3", "16:9", "16:10", "21:9" },
                      { "4:3", "16:9", "16:10", "21:9" }, 4 },
    /* FIELD OF VIEW (night order 2026-11-07; THPS 2X v1.2.0: ORIGINAL, +5 .. +30 degrees): added to the camera angle
     * the game hands its renderer (sub_000419A0), at once. The menu font has no '+': the value is the degrees. */
    [RV_FOV]      = { "FIELD OF VIEW", "field_of_view", "0", { "0", "5", "10", "15", "20", "25", "30" },
                      { "ORIGINAL", "5", "10", "15", "20", "25", "30" }, 7 },
    /* SPEAKERS (night order 2026-11-07, item 6; in SOUND, not RECOMP OPTIONS - maintainer): 5.1 = the game's own six
     * speaker feeds to a 5.1 device (main.c RECOMP_AUDIO_51 -> toolkit XC_AUDIO Dolby Digital), at start */
    [RV_SPEAKERS] = { "SPEAKERS", "speakers", "stereo", { "stereo", "5.1" }, { "STEREO", "5.1" }, 2 },
    [RV_ANISO]    = { "ANISOTROPIC FILTER", "anisotropic_filter", "16", { "off", "2", "4", "8", "16" },
                      { "OFF", "2X", "4X", "8X", "16X" }, 5 },
    /* SYSTEM = the host's UI language if the disc has it (toolkit xbox_settings, else English) - as before this line */
    [RV_LANGUAGE] = { "LANGUAGE", "language", "system",
                      { "system", "english", "german", "french", "spanish", "italian", "japanese" },
                      { "SYSTEM", "ENGLISH", "DEUTSCH", "FRANCAIS", "ESPANOL", "ITALIANO", "JAPANESE" }, 7 },
};
/* The port views' lines: a value line (left / right / Y), a sub-list (A opens view a), a pick (A sets value b of
 * k_rv[a]), a plain text. */
enum { PL_NONE, PL_VAL, PL_SUB, PL_PICK, PL_TEXT, PL_GAMEF };   /* PL_GAMEF: a game float, a = 0 / 1 below */
typedef struct { unsigned char kind, a, b; const char *text; } opt_pl_t;
static const opt_pl_t k_pl[OPT_V_N][9] = {
    /* order: maintainer 2026-11-05 - BRIGHTNESS AND CONTRAST first, BOOT ANIMATION last (k_recomp help in the same order) */
    [OPT_V_RECOMP] = { { PL_SUB, OPT_V_PIC, 0, "BRIGHTNESS AND CONTRAST" },
                       /* ASPECT RATIO AND FOV > (night 2026-11-07): the view has nine stock lines, all in use - FIELD
                        * OF VIEW joins ASPECT RATIO in a sub-list (THPS 2X has it right under ASPECT RATIO) */
                       { PL_VAL, RV_DISPLAY }, { PL_VAL, RV_RENDER }, { PL_SUB, OPT_V_VIEW, 0, "ASPECT RATIO AND FOV" },
                       { PL_VAL, RV_ANISO },
                       { PL_SUB, OPT_V_AA, 0, "ANTIALIASING" }, { PL_SUB, OPT_V_FPS, 0, "FRAME RATE" },
                       { PL_VAL, RV_LANGUAGE }, { PL_SUB, OPT_V_BOOT, 0, "BOOT ANIMATION" } },
    [OPT_V_AA]     = { { PL_PICK, RV_AA, 0 }, { PL_PICK, RV_AA, 1 }, { PL_PICK, RV_AA, 2 }, { PL_PICK, RV_AA, 3 },
                       { PL_PICK, RV_AA, 4 } },
    [OPT_V_FPS]    = { { PL_VAL, RV_FRAMERATE }, { PL_VAL, RV_PACING }, { PL_VAL, RV_VSYNC }, { PL_VAL, RV_FPSHUD } },
    [OPT_V_BOOT]   = { { PL_TEXT, 1 }, { PL_VAL, RV_BOOT } },   /* a = 1: the file's status line */
    /* leaving RECOMP OPTIONS with a restart-bound value changed (CLAUDE.md RESTART NEEDED; THPS 2X host_restart) */
    [OPT_V_RESTART] = { { PL_TEXT, 0, 0, "RESTART NEEDED" }, { PL_TEXT, 3, 0, "RESTART NOW" },
                        { PL_TEXT, 4, 0, "LATER" } },
    [OPT_V_PIC]    = { { PL_GAMEF, 0, 0, "BRIGHTNESS" }, { PL_GAMEF, 1, 0, "CONTRAST" } },
    [OPT_V_VIEW]   = { { PL_VAL, RV_ASPECT }, { PL_VAL, RV_FOV } },
    /* SOUND: the game's three volumes (stock lines 3-5) + SPEAKERS on stock line 6 (ANZEIGE SICHTBAR, its value
     * sprites parked) - maintainer 2026-11-07 ~23:45: the 5.1 choice in the game's SOUND options */
    [OPT_V_SOUND]  = { [6] = { PL_VAL, RV_SPEAKERS } },
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
    return (view >= OPT_V_RECOMP || view == OPT_V_SOUND) && view < OPT_V_N && line >= 0 && line < 9
               ? &k_pl[view][line] : NULL;
}

/* a line the port owns: every line of its own views, and a port value line inside a stock view (SOUND > SPEAKERS) */
static int opt_port_line(int view, int line)
{
    const opt_pl_t *pl = opt_pl(view, line);
    return view >= OPT_V_RECOMP || (pl && pl->kind != PL_NONE);
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

/* FIELD OF VIEW in Orta's angle units (65536 = one turn), -1 = read the setting again (sub_000419A0 below) */
static int s_fov_units = -1;

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
    if (s_ws_start < 0) s_ws_start = strcmp(settings_get("aspect_ratio", "4:3"), "4:3") != 0;
    return s_ws_start;
}
/* The aspect the RENDER RESOLUTION line lists: the SAVED one (THPS 2X v1.2.0: after an ASPECT RATIO change the line
 * lists the new aspect's sizes right away; they apply after the restart, with the aspect). The wide aspects (16:9,
 * 16:10, 21:9) share one key and one list of line counts; the width follows the aspect. */
static int rv_ws_menu(void) { return strcmp(settings_get("aspect_ratio", "4:3"), "4:3") != 0; }
/* 21:9 = the display's EXACT ratio while it is in the 21:9 range (2026-11-08, all ports; toolkit option-c
 * b95eec8: xbox_video_aspect() gives 43:18 on a 3440x1440 display, 64:27 on 2560x1080 or outside the
 * range). xbox_video_aspect() is fixed once the window is open; when this process does not run at 21:9 (the menu lists
 * a 21:9 chosen but not yet restarted) it is not a 21:9 value -> 64:27 until the restart. */
static double orta_aspect_21x9(void)
{
    double r = xbox_video_aspect();
    return (r >= 2.30 && r <= 2.42) ? r : 64.0 / 27.0;
}
static double rv_aspect_menu(void)
{
    const char *a = settings_get("aspect_ratio", "4:3");
    return !strcmp(a, "16:10") ? 1.6 : !strcmp(a, "21:9") ? orta_aspect_21x9() : !strcmp(a, "16:9") ? 16.0 / 9.0 : 4.0 / 3.0;
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
    if (id == RV_FOV) s_fov_units = -1;          /* the camera call picks the new value up (at once) */
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
        int ws = rv_ws_menu(), s = rv_render_step(ws, NULL);
        double f = rv_render_factor(s, ws);
        /* wide: lines x aspect, rounded up from x.25 on (854X480 as CLAUDE.md / THPS 2X; 1280, 1920, 2560X1080 ...) */
        int h = (int)(480 * f + 0.5), w = ws ? (int)ceil(h * rv_aspect_menu() - 0.25) : (int)(640 * f + 0.5);
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
        int ws = rv_ws_menu(), s = rv_render_step(ws, NULL) + dir;
        char v[16];
        if (s < 0 || s > rv_render_last(ws)) return 0;
        if (!nv2a_vk_render_scale_fits(rv_render_factor(s, ws), ws)) return 0;   /* refused: the device cannot hold it */
        snprintf(v, sizeof v, "%.3f", rv_render_factor(s, ws));
        settings_set(rv_render_key(ws), v);
        if (ws == rv_ws()) nv2a_vk_set_render_scale(rv_render_factor(s, ws), ws);   /* else: after the restart */
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
        int ws = rv_ws_menu();
        if (rv_render_step(ws, NULL) == 0) return 0;
        settings_set(rv_render_key(ws), "1.000");
        if (ws == rv_ws()) nv2a_vk_set_render_scale(1.0, ws);
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
    s_rv_start[RV_PACING] = rv_index(RV_PACING);
    s_rv_start[RV_FRAMERATE] = rv_index(RV_FRAMERATE);
    s_rv_start[RV_SPEAKERS] = rv_index(RV_SPEAKERS);
    {
        extern void orta_diag_player_start_ext(void);
        orta_diag_player_start_ext();
    }
    {
        /* FRAME RATE 120 (item 7): Orta steps its world by the frame time in APPDATA 0x341E30 (1/60; read 186 times
         * by fld / fmul / fdiv, never written by address) - with the guest vblank at 120 Hz (main.c) it becomes
         * 1/120, so the time based parts keep their speed. ORTA_FPS=120 / 60 forces it for one run. */
        const char *e = getenv("ORTA_FPS");
        int fps120 = e && *e ? atoi(e) == 120 : rv_index(RV_FRAMERATE) == 1;
        if (fps120) {
            float dt = 1.0f / 120.0f;
            uint32_t bits;
            memcpy(&bits, &dt, 4);
            fprintf(stderr, "[ORTA] frame rate 120: frame time step 0x341E30 %08X -> %08X\n",
                    (unsigned)GUEST32(0x341E30u), (unsigned)bits);
            GUEST32(0x341E30u) = bits;
        }
    }
    rv_apply(RV_DISPLAY);
    rv_apply(RV_ANISO);
    rv_apply(RV_AA);
    rv_apply(RV_FPSHUD);
    fprintf(stderr, "[OPT] options: display %s, render scale %s, aspect %s, anisotropic filter %dx, antialiasing %s, "
            "fps counter %s, vsync %s, language %s, frame pacing %s, field of view %s, frame rate %s, speakers %s\n",
            k_rv[RV_DISPLAY].val[rv_index(RV_DISPLAY)], saved ? settings_get(rv_render_key(ws), "") : "default",
            k_rv[RV_ASPECT].val[rv_index(RV_ASPECT)], nv2a_vk_aniso, k_rv[RV_AA].val[rv_index(RV_AA)],
            k_rv[RV_FPSHUD].val[rv_index(RV_FPSHUD)], k_rv[RV_VSYNC].val[rv_index(RV_VSYNC)],
            k_rv[RV_LANGUAGE].val[rv_index(RV_LANGUAGE)], k_rv[RV_PACING].val[rv_index(RV_PACING)],
            k_rv[RV_FOV].val[rv_index(RV_FOV)], k_rv[RV_FRAMERATE].val[rv_index(RV_FRAMERATE)],
            k_rv[RV_SPEAKERS].val[rv_index(RV_SPEAKERS)]);
}

/* ── Debug: the dragon object over time (2026-11-07; finding the special gauge for the debug cheats) ─────
 * ORTA_DIAG_PLAYER=<file>: a host thread writes, once a second, a record "ORTP" + ms + the object pointer + 0x4000
 * bytes of the dragon object [0x474230] (health +0x3DC, max +0x3E0: the HUD's sub_0002EA90; +0x06C frames in the
 * current section, +0x468 invulnerability frames after a hit) to <file>. Off by default; its own thread, never the
 * game's. tools/player_diff.py reads it. */
#include <SDL3/SDL.h>
static int SDLCALL orta_diag_player_thread(void *path)
{
    FILE *f = fopen((const char *)path, "wb");
    static uint8_t buf[0x4000];
    if (!f) return 0;
    for (;;) {
        uint32_t p = GUEST32(0x474230u), ms = (uint32_t)SDL_GetTicks(), i;
        SDL_Delay(1000);
        if (p < 0x10000u || p > 0x3FF0000u) continue;
        for (i = 0; i < sizeof buf; i += 4) { uint32_t w = GUEST32(p + i); memcpy(buf + i, &w, 4); }
        fwrite("ORTP", 1, 4, f); fwrite(&ms, 4, 1, f); fwrite(&p, 4, 1, f); fwrite(buf, 1, sizeof buf, f);
        fflush(f);
    }
}
/* ORTA_DIAG_POKE=<offset hex>:<float> (diagnosis): every 100 ms that float into the dragon object - which field is
 * which gauge (a cheat candidate held full, then watched on screen). */
static int SDLCALL orta_diag_poke_thread(void *spec)
{
    const char *s = (const char *)spec;
    uint32_t off = (uint32_t)strtoul(s, (char **)&s, 16), bits;
    float v = *s == ':' ? (float)atof(s + 1) : 0.0f;
    memcpy(&bits, &v, 4);
    for (;;) {
        uint32_t p = GUEST32(0x474230u);
        if (p >= 0x10000u && p < 0x3FF0000u) GUEST32(p + off) = bits;
        SDL_Delay(100);
    }
}
/* ── Debug cheats (2026-11-07: for debugging and automated test runs only - never a menu line, never in the
 * README / release notes; default off) ──────────────────────────────────────────────────────────────────────────────
 * The dragon object [0x474230] (found 2026-11-07 from a recorded session with timed boost / special presses): health float +0x3DC, max +0x3E0; special gauge float +0x4D0 (0 .. 7200 = full, fills
 * with kills, drains in ~3 s when White fires it); form int +0x01C (Y cycles 0 -> 1 -> 2 -> 0; 2 = the small dragon);
 * +0x06C frames in the current section (counts while flying). The object is made anew after a death - the pointer is
 * read every time.
 *   ORTA_CHEAT_HEALTH=1   health held at its max (every 100 ms; hits of 10-15 never reach 0 in between)
 *   ORTA_CHEAT_SPECIAL=1  the special gauge held full
 *   ORTA_CHEAT_FORM=<n>   while the section counter runs and the form is not n: Y tapped (the keyboard's I key, the
 *                         default layout in input.c), 1.5 s between taps for the morph - the autoplay's "Y x2 at each
 *                         level start" (tools/autoplay_script.py)
 * One host thread, never the game's; writes only these fields. */
extern void xbox_InputKeyEvent(int scancode, int down);
static int SDLCALL orta_cheat_thread(void *unused)
{
    const char *eh = getenv("ORTA_CHEAT_HEALTH"), *es = getenv("ORTA_CHEAT_SPECIAL"), *ef = getenv("ORTA_CHEAT_FORM");
    const char *eb = getenv("ORTA_CHEAT_BOOST"), *ew = getenv("ORTA_CHEAT_WIGGLE");
    int health = eh && *eh == '1', special = es && *es == '1', form = ef && *ef ? atoi(ef) : -1;
    int boost = eb && *eb == '1';
    /* ORTA_CHEAT_WIGGLE=<ms> (2026-11-08): while the section counter runs (in flight - not in menus, cut scenes, the
     * score screen or the save question), the stick up and down by turns, <ms> each, through the keyboard's W / S
     * (input.c keymap) - episode 2's boss wants "up or down". In the port: a first version steered from window
     * captures, and a window capture every 2 s stalled the renderer 0.3-1.2 s. */
    int wiggle = ew && *ew ? atoi(ew) : 0, wkey = 0;
    uint32_t last_w = 0;
    /* ORTA_CHEAT_MODE=<file> (2026-11-08; a second test mode, switched while the game runs): the file's first word,
     * read every 0.5 s - "boost" = X on turbo (keyboard J, input.c keymap: 120 ms down every 500 ms; one tap per cycle
     * never boosted) + the small-dragon form (ORTA_CHEAT_FORM) also while the pad script is paused; anything else =
     * normal. The test driver pauses the pad script (RECOMP_PAD_LIVE "pause") for it. */
    const char *em = getenv("ORTA_CHEAT_MODE");
    int mode_boost = 0, xdown = 0;
    uint32_t last_mode = 0, last_x = 0;
    uint32_t last_tap = 0, last_sec = 0, sec_same = 0, boost_ptr = 0;
    float boost_max = 0.0f;
    float full = 7200.0f;
    uint32_t full_bits;
    (void)unused;
    memcpy(&full_bits, &full, 4);
    fprintf(stderr, "[CHEAT] health %d, special %d, boost %d, form %d, wiggle %d ms\n", health, special, boost, form,
            wiggle);
    for (;;) {
        uint32_t p = GUEST32(0x474230u), now = (uint32_t)SDL_GetTicks();
        SDL_Delay(100);
        if (em && *em && now - last_mode >= 500u) {
            FILE *mf = fopen(em, "rb");
            char w[16] = {0};
            int nb = 0;
            last_mode = now;
            if (mf) {
                if (fscanf(mf, "%15s", w) != 1) w[0] = 0;
                fclose(mf);
            }
            nb = strcmp(w, "boost") == 0;
            if (nb != mode_boost) {
                mode_boost = nb;
                fprintf(stderr, "[CHEAT] mode %s\n", nb ? "BOOST (X turbo + small dragon)" : "normal");
            }
        }
        if (mode_boost && now - last_x >= (xdown ? 120u : 380u)) {
            xdown = !xdown;
            xbox_InputKeyEvent(13, xdown);                     /* HID J = X */
            last_x = now;
        } else if (!mode_boost && xdown) {
            xbox_InputKeyEvent(13, 0);
            xdown = 0;
        }
        if (p < 0x10000u || p > 0x3FF0000u) continue;
        if (health && GUEST32(p + 0x3E0u) != 0u) GUEST32(p + 0x3DCu) = GUEST32(p + 0x3E0u);
        if (special) GUEST32(p + 0x4D0u) = full_bits;
        if (boost) {
            /* +0x484: boost charges (small dragon 3.0 = three boosts, refills slowly); held at the highest value seen
             * for this object (forms may have another count) */
            uint32_t w = GUEST32(p + 0x484u), mb;
            float b;
            memcpy(&b, &w, 4);
            if (p != boost_ptr) { boost_ptr = p; boost_max = 0.0f; }
            if (b == b && b > boost_max && b < 16.0f) boost_max = b;
            if (boost_max > 0.0f && b < boost_max) { memcpy(&mb, &boost_max, 4); GUEST32(p + 0x484u) = mb; }
        }
        if (form >= 0 || wiggle > 0) {
            uint32_t sec = GUEST32(p + 0x06Cu);
            sec_same = sec == last_sec ? sec_same + 1 : 0;      /* standing still: menu, pause, cut scene */
            last_sec = sec;
        }
        if (wiggle > 0) {
            /* the pad script's pause switch (BACK held 1.5 s / "pause" in the live file) stops the wiggle too: the
             * maintainer plays alone */
            int flying = sec_same == 0 && GUEST32(p + 0x06Cu) > 300u && !xbox_InputScriptPaused();
            if (!flying && wkey) {                              /* let go outside flight */
                xbox_InputKeyEvent(wkey, 0);
                wkey = 0;
            } else if (flying && now - last_w >= (uint32_t)wiggle) {
                int next = wkey == 26 ? 22 : 26;                /* HID W (stick up) <-> S (stick down) */
                if (wkey) xbox_InputKeyEvent(wkey, 0);
                xbox_InputKeyEvent(next, 1);
                wkey = next;
                last_w = now;
            }
        }
        if (form >= 0) {
            /* 4 s between taps and 5 s into a section: the morph takes longer than 1.5 s (run 20261107 autoplay_long40:
             * a tap while the first morph still ran went one form too far), and the first level's recorded Y x2 comes
             * right at its start */
            /* only the three dragon forms 0..2 (Y cycles them): episode 5 is a special episode without them - the
             * object there reads form 3 and the cheat tapped Y over and over (run 20261110_124558_issue1_auto_ep5) */
            int cur_ = (int)GUEST32(p + 0x01Cu);
            if (cur_ >= 0 && cur_ <= 2 && sec_same == 0 && last_sec > 300u && cur_ != form && now - last_tap > 4000u
                && (!xbox_InputScriptPaused() || mode_boost)) {
                xbox_InputKeyEvent(12, 1);                    /* HID I = Y */
                SDL_Delay(120);
                xbox_InputKeyEvent(12, 0);
                last_tap = now;
                fprintf(stderr, "[CHEAT] form %d -> Y\n", (int)GUEST32(p + 0x01Cu));
            }
        }
    }
}
static void orta_diag_player_start(void)
{
    const char *e = getenv("ORTA_DIAG_PLAYER"), *k = getenv("ORTA_DIAG_POKE");
    const char *c1 = getenv("ORTA_CHEAT_HEALTH"), *c2 = getenv("ORTA_CHEAT_SPECIAL"), *c3 = getenv("ORTA_CHEAT_FORM");
    const char *c4 = getenv("ORTA_CHEAT_BOOST"), *c5 = getenv("ORTA_CHEAT_WIGGLE");
    if (e && *e) SDL_DetachThread(SDL_CreateThread(orta_diag_player_thread, "orta_diag_player", (void *)e));
    if (k && *k) SDL_DetachThread(SDL_CreateThread(orta_diag_poke_thread, "orta_diag_poke", (void *)k));
    if ((c1 && *c1) || (c2 && *c2) || (c3 && *c3) || (c4 && *c4) || (c5 && *c5))
        SDL_DetachThread(SDL_CreateThread(orta_cheat_thread, "orta_cheat", NULL));
}
void orta_diag_player_start_ext(void) { orta_diag_player_start(); }

/* ── ASPECT RATIO 16:10 / 21:9 and FIELD OF VIEW (night order 2026-11-07) ───────────────────────────────────────────
 * Orta's own widescreen (the console's XC_VIDEO widescreen bit; flag copy 0x342754) widens the picture by one factor,
 * the float 4/3 at 0x30993C, in four places that test the flag:
 *   sub_0003D340  display set-up: the renderer's aspect 0x342724 = 1.0 (x 4/3 when wide) -> renderer vtable +0x208
 *   sub_000419A0  camera: half angle MEM32(0x449794) (65536 = one turn) -> tan x 4/3 when wide -> atan -> the wider
 *                 angle (stored 0x4497C0, renderer vtable +0x7C)
 *   sub_0002FC50, sub_000B7670  further users of the same factor under the same flag
 * (0x30993C is read by six functions, found in the generated C; sub_00015790 and sub_0015B200 use it without the
 * flag and are left alone.) For 16:10 / 21:9 those four run with the factor aspect / (4/3) = 1.2 / 1.7778 in place
 * of 4/3 - Hor+: the height stays, more is seen at the sides. 4:3 and 16:9 run unchanged.
 * FIELD OF VIEW adds its degrees to the camera angle for the call of sub_000419A0 only (the game's own value is put
 * back after it), so the game's zooms keep working on top. */
static float s_aspect_factor = -1.0f;             /* 0 = stock (4:3 / 16:9), else the factor for 0x30993C */
static int s_aspect_is_21x9;
static float orta_aspect_factor(void)
{
    if (s_aspect_factor < 0.0f) {
        const char *a = settings_get("aspect_ratio", "4:3");     /* the value this process started with (main.c) */
        s_aspect_is_21x9 = !strcmp(a, "21:9");
        s_aspect_factor = !strcmp(a, "16:10") ? 1.2f : s_aspect_is_21x9 ? (float)(orta_aspect_21x9() * 0.75) : 0.0f;
        fprintf(stderr, "[ORTA] aspect %s: widescreen factor %.4f\n", a, s_aspect_factor > 0.0f ? s_aspect_factor : 4.0 / 3.0);
    } else if (s_aspect_is_21x9) {
        /* the display fit happens when the window opens: read it at use time, never only the value from before it */
        float f = (float)(orta_aspect_21x9() * 0.75);
        if (f != s_aspect_factor) {
            fprintf(stderr, "[ORTA] aspect 21:9: widescreen factor %.4f (display ratio %.4f)\n", f, f / 0.75);
            s_aspect_factor = f;
        }
    }
    return s_aspect_factor;
}
#define ORTA_WS_FACTOR_CALL(genfn)                                                                                  \
    do {                                                                                                            \
        float f_ = orta_aspect_factor();                                                                            \
        if (f_ > 0.0f) {                                                                                            \
            uint32_t keep_ = GUEST32(0x30993Cu), bits_;                                                            \
            memcpy(&bits_, &f_, 4);                                                                                 \
            GUEST32(0x30993Cu) = bits_;                                                                             \
            genfn(ctx, base);                                                                                       \
            GUEST32(0x30993Cu) = keep_;                                                                             \
        } else                                                                                                      \
            genfn(ctx, base);                                                                                       \
    } while (0)

extern void sub_0003D340_gen(RECOMP_PARAMS);
extern void sub_000419A0_gen(RECOMP_PARAMS);
extern void sub_0002FC50_gen(RECOMP_PARAMS);
extern void sub_000B7670_gen(RECOMP_PARAMS);
/* The HUD: at 16:9 the game squeezes its 2D around the centre, x = (x - 320) * 0.75 + 320 (0.75 = the float at
 * 0x309728 = (4/3) / (16/9)), in the functions that test the widescreen flag 0x342754 and read that float (found in
 * the generated C: sub_00011C50, sub_000327E0, sub_00069A90; 0x309728 has other users without the flag, left
 * alone). For 16:10 / 21:9 those run with (4/3) / aspect = the widescreen factor's inverse - the HUD at 21:9 was a
 * third too wide (run 20261107_222652_shots_a219: SCORE gauge, radar oval). */
#define ORTA_WS_SQUEEZE_CALL(genfn)                                                                                 \
    do {                                                                                                            \
        float f_ = orta_aspect_factor();                                                                            \
        if (f_ > 0.0f) {                                                                                            \
            float q_ = 1.0f / f_;                                                                                   \
            uint32_t keep_ = GUEST32(0x309728u), bits_;                                                            \
            memcpy(&bits_, &q_, 4);                                                                                 \
            GUEST32(0x309728u) = bits_;                                                                             \
            genfn(ctx, base);                                                                                       \
            GUEST32(0x309728u) = keep_;                                                                             \
        } else                                                                                                      \
            genfn(ctx, base);                                                                                       \
    } while (0)
extern void sub_00011C50_gen(RECOMP_PARAMS);
extern void sub_000327E0_gen(RECOMP_PARAMS);
extern void sub_00069A90_gen(RECOMP_PARAMS);
void sub_00011C50(RECOMP_PARAMS) { ORTA_WS_SQUEEZE_CALL(sub_00011C50_gen); }
void sub_000327E0(RECOMP_PARAMS) { ORTA_WS_SQUEEZE_CALL(sub_000327E0_gen); }
/* HUD to the edges (port rule: HUD anchored to the screen edges of the wider view; night order 2026-11-07).
 * sub_00069A90 is the 2D sprite emitter: it writes one quad (4 vertices, stride 0x20, x at +0) at the vertex pointer
 * 0x46F890, advances it by 0x80 and - widescreen flag 0x342754 set and sprite flag byte [arg+2] bit 3 clear - squeezes
 * the four x around 320 (above). The squeeze alone keeps the 16:9 layout's distance from the centre, so at 21:9 the
 * gauge / radar sat 80 px (of 640) inside the edge. Moving each quad on its own tore text apart (maintainer
 * 2026-11-08: the episode title / subtitles at a level's start, "DIE S   TADT IM ST   URM", run
 * 20261108_005700_intro_a219: one glyph = one quad). So the move works on GROUPS: sub_0006A740 (the sprite layer)
 * emits the quads of one texture into the buffer 0x46F894..[0x46F890] and draws them with sub_0006A520, which then
 * resets the pointer. sub_00069A90 notes each squeezed quad; before the draw, sub_0006A520's override sorts them by
 * x and joins quads closer than 32 px (16:9 layout) into groups - a text line, the gauge with its bars, the radar with
 * its dots. A group is moved by d = 320 * (1 - k), k = (16/9) / aspect, outwards when its centre (16:9 layout) lies
 * in the left / right third and its width is at most 60 % (THPS 2X's anchor rule): the same distance to the edge as
 * at 16:9, the same width; 16:10 (k > 1) moves it inwards; centre groups stay. Not while 0x345DD0 == 1 (a second
 * squeeze, 0x30DBA8, a mode not seen in play yet). sub_00011C50 / sub_000327E0 draw on the stack, stay centre-squeezed. */
#define ORTA_HUD_MAXQ 1024
static uint32_t s_hud_q[ORTA_HUD_MAXQ];            /* guest address of each squeezed quad of the pending batch */
static int s_hud_n;

static float orta_qx(uint32_t va) { uint32_t b = GUEST32(va); float v; memcpy(&v, &b, 4); return v; }

void sub_00069A90(RECOMP_PARAMS)
{
    float f = orta_aspect_factor();
    uint32_t arg = GUEST32(g_esp + 4), vp0 = GUEST32(0x46F890u);
    ORTA_WS_SQUEEZE_CALL(sub_00069A90_gen);
    if (!(f > 0.0f) || !GUEST32(0x342754u) || (GUEST8(arg + 2u) & 8u) || GUEST32(0x345DD0u) == 1u)
        return;
    if (GUEST32(0x46F890u) == vp0 + 0x80u && s_hud_n < ORTA_HUD_MAXQ)
        s_hud_q[s_hud_n++] = vp0;
}

extern void sub_0006A520_gen(RECOMP_PARAMS);
void sub_0006A520(RECOMP_PARAMS)
{
    float f = orta_aspect_factor();
    int n = s_hud_n, i, j;
    uint32_t lo = GUEST32(0x46F894u), hi = GUEST32(0x46F890u);
    s_hud_n = 0;
    if (f > 0.0f && n > 0) {
        static float bx0[ORTA_HUD_MAXQ], bx1[ORTA_HUD_MAXQ];
        static uint32_t bq[ORTA_HUD_MAXQ];
        float s16 = 0.75f * f;                       /* fb px -> 16:9 layout px (0.75 / q, q = 1 / f) */
        float k = 4.0f / (3.0f * f), dmove = 320.0f * (1.0f - k);
        int m = 0;
        for (i = 0; i < n; i++) {                    /* this batch's quads only, sorted by x0 (insertion) */
            uint32_t va = s_hud_q[i];
            float a = 1e30f, b = -1e30f;
            if (va < lo || va + 0x80u > hi)
                continue;
            for (j = 0; j < 4; j++) {
                float x = orta_qx(va + (uint32_t)j * 0x20u);
                if (x < a) a = x;
                if (x > b) b = x;
            }
            for (j = m; j > 0 && bx0[j - 1] > a; j--) {
                bx0[j] = bx0[j - 1]; bx1[j] = bx1[j - 1]; bq[j] = bq[j - 1];
            }
            bx0[j] = a; bx1[j] = b; bq[j] = va;
            m++;
        }
        for (i = 0; i < m;) {                        /* groups: next quad starts within 32 px of the group's right end */
            float g0 = bx0[i], g1 = bx1[i], d = 0.0f, c16;
            for (j = i + 1; j < m && (bx0[j] - g1) * s16 <= 32.0f; j++)
                if (bx1[j] > g1) g1 = bx1[j];
            c16 = 320.0f + (0.5f * (g0 + g1) - 320.0f) * s16;
            if ((g1 - g0) * s16 <= 384.0f) {
                if (c16 < 640.0f / 3.0f) d = -dmove;
                else if (c16 > 1280.0f / 3.0f) d = dmove;
            }
            if (d != 0.0f) {
                int t, v;
                for (t = i; t < j; t++)
                    for (v = 0; v < 4; v++) {
                        uint32_t va = bq[t] + (uint32_t)v * 0x20u, b;
                        float x = orta_qx(va) + d;
                        memcpy(&b, &x, 4);
                        GUEST32(va) = b;
                    }
            }
            i = j;
        }
    }
    sub_0006A520_gen(ctx, base);
}

void sub_0003D340(RECOMP_PARAMS) { ORTA_WS_FACTOR_CALL(sub_0003D340_gen); }
void sub_0002FC50(RECOMP_PARAMS) { ORTA_WS_FACTOR_CALL(sub_0002FC50_gen); }
void sub_000B7670(RECOMP_PARAMS) { ORTA_WS_FACTOR_CALL(sub_000B7670_gen); }

void sub_000419A0(RECOMP_PARAMS)
{
    uint32_t keep = GUEST32(0x449794u);
    if (s_fov_units < 0) s_fov_units = (int)(atoi(k_rv[RV_FOV].val[rv_index(RV_FOV)]) * 65536.0 / 360.0 + 0.5);
    if (s_fov_units) GUEST32(0x449794u) = keep + (uint32_t)s_fov_units;
    ORTA_WS_FACTOR_CALL(sub_000419A0_gen);
    if (s_fov_units) GUEST32(0x449794u) = keep;
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
    if (opt_port_line(view, line)) { rv_text(view, line, buf, n); return buf; }
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
        char stock[48], low[48];
        const char *s = t;
        /* Lower case = the font's small capitals, the size of the game's own option labels (ANZEIGE SICHTBAR,
         * VIBRATION); upper case is its big capitals - PRACTICE, SOUND, DISPLAY MODE ... looked bigger (maintainer
         * 2026-11-05: "manche sind größer als andere" -> all small, as EXIT GAME in the main menu). Digits, ':' and
         * the rest keep their codes. */
        if (t) {
            size_t i;
            for (i = 0; t[i] && i + 1 < sizeof low; i++) low[i] = (t[i] >= 'A' && t[i] <= 'Z') ? (char)(t[i] + 32) : t[i];
            low[i] = 0;
            s = low;
        }
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
        /* TOP's PRACTICE is stock line 0 (AIMING): its value stays parked */
        if (k_opt_values[view] && !(view == OPT_V_TOP && line == 0) && !opt_port_line(view, line)) {
            moved += opt_shift_line(line, s_opt_vrow[line], row, OPT_MV_VALUE); s_opt_vrow[line] = row;
        }
        opt_line_text(line, t);
    }
    {
        static const char *const k_name[OPT_V_N] = { "TOP", "CONTROLS", "SOUND", "RECOMP", "RECOMP > ANTIALIASING",
                                                     "RECOMP > FRAME RATE", "RECOMP > BOOT ANIMATION",
                                                     "CONTROLS > BUTTON MAPPING", "RESTART NEEDED",
                                                     "RECOMP > BRIGHTNESS AND CONTRAST",
                                                     "RECOMP > ASPECT RATIO AND FOV" };
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
    uint32_t id = GUEST32(g_esp + 4), inst, b, caller = GUEST32(g_esp);
    static int diag = -1;
    if (diag < 0) diag = getenv("ORTA_DIAG_CREATE") != NULL;
    sub_001BB610_gen(ctx, base);
    inst = g_eax;
    if (diag) fprintf(stderr, "[CREATE] id 0x%X from 0x%08X -> %08X\n", id, caller, inst);
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
    static const char *const k_recomp[9] = {        /* = the k_pl[OPT_V_RECOMP] order */
        "The game's brightness and contrast.\nPress A to open.",
        "Fullscreen, borderless or windowed.\nLeft or right to change, Y for the default.\nApplies at once.",
        "The game's 480 lines times this factor.\nLeft or right to change, Y for the default.\nAt once; a new aspect ratio after restart.",
        "Aspect ratio and field of view.\nPress A to open.",
        "Sharper textures seen at an angle.\nLeft or right to change, Y for the default.\nApplies at once.",
        "Edge smoothing.\nPress A to open.",
        "Frame rate, VSync and FPS counter.\nPress A to open.",
        "Game language. SYSTEM follows Windows.\nLeft or right to change, Y for the default.\nNeeds a restart.",
        "Boot animation from media\\boot.mpg.\nPress A to open.",
    };
    static const char *const k_fps[4] = {
        "ORIGINAL 60 as the console.\n120 follows once the game logic runs apart from it.",
        "DISPLAY: even frames on 120/180/240 Hz.\nTIMER: the old 60 Hz clock.\nLeft or right, Y for the default. Needs a restart.",
        "Waits for the display.\nLeft or right to change, Y for the default.\nNeeds a restart.",
        "Frames per second and frame time on screen.\nLeft or right to change, Y for the default.\nApplies at once.",
    };
    if (line > 8) return NULL;
    if (view == OPT_V_RECOMP) return k_recomp[line];
    if (view == OPT_V_AA)
        return "Smooths jagged edges.\nA to choose, Y for the default.\nApplies at once.";
    if (view == OPT_V_FPS && line < 4) return k_fps[line];
    if (view == OPT_V_BOOT)
        return "Plays media\\boot.mpg at every start.\nLeft or right to change, Y for the default.\nSee the README.";
    if (view == OPT_V_TOP && line == 0) return "The practice missions: flying, aiming, dragon forms.\nPress A to start.";
    if (view == OPT_V_TOP && line == 2) return "Aiming, vibration and button mapping.\nPress A to open.";
    if (view == OPT_V_TOP && line == 7) return "Music, effects, voice volume and speakers.\nPress A to open.";
    if (view == OPT_V_SOUND && line == 6)
        return "5.1: the game's own surround mix, on a 5.1 device.\nLeft or right to change, Y for the default.\nNeeds a restart.";
    if (view == OPT_V_TOP && line == 8) return "Display and graphics settings of the recomp.\nPress A to open.";
    if (view == OPT_V_CONTROLS && line == 0)   /* the game's help names the old label AUFW./ABW. */
        return "Up and down for aiming: normal or inverted.\nLeft or right to change, Y for the default.";
    if (view == OPT_V_CONTROLS && line == 2)
        return "Keys and buttons for the keyboard or the controller.\nPress A to open.";
    if (view == OPT_V_VIEW)
        return line == 0 ? "4:3 as the original, 16:9, 16:10 or 21:9.\nWider shows more at the sides.\nLeft or right, Y for the default. Needs a restart."
                         : "Degrees wider than the original camera.\nLeft or right to change, Y for the default.\nApplies at once.";
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
    if ((bits & 0x8u) && (s_opt_view == OPT_V_CONTROLS || s_opt_view == OPT_V_SOUND) &&
        !opt_port_line(s_opt_view, (int)cur)) {
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
    if ((bits & 0x8u) && opt_port_line(s_opt_view, (int)cur)) {   /* Y (0x00000008, run _094159_opt_views16): default */
        if (rv_default(s_opt_view, (int)cur)) {
            opt_view_texts(s_opt_view);
            fprintf(stderr, "[OPT] view %d line %u: default\n", s_opt_view, cur);
        }
    }
    if (s_opt_view == OPT_V_TOP && cur == 0 && (bits & (0x101u | 0x08000000u | 0x04000000u))) {
        /* PRACTICE: left / right would switch the hidden AIMING value - swallowed. A = leave OPTIONS the game's own
         * way (B at TOP: saves the settings, back to the main menu), which then starts ÜBUNG (sub_000C0280). */
        extern int g_orta_practice_request;
        uint32_t keep = bits & ~(0x101u | 0x08000000u | 0x04000000u);
        if (bits & 0x101u) {
            g_orta_practice_request = 1;
            keep |= 0x202u;
            fprintf(stderr, "[OPT] PRACTICE: leaving OPTIONS for ÜBUNG\n");
        }
        GUEST32(obj + 0x18) = keep;
        g_ecx = obj;
        sub_000AF580_gen(ctx, base);
        GUEST32(obj + 0x18) = bits;
        return;
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
        swallow = to >= 0 || (s_opt_view == OPT_V_CONTROLS && cur == 2) || opt_port_line(s_opt_view, (int)cur);
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
    if ((bits & (0x08000000u | 0x04000000u)) && opt_port_line(s_opt_view, (int)cur)) {   /* left / right: our values */
        /* 0x04000000 = LEFT, 0x08000000 = RIGHT (run 20261104_090544_opt_views14: a scripted left arrived as
         * 0x44004000; down = 0x22002000, B = 0x00000002) */
        int dir = (bits & 0x08000000u) ? 1 : -1;
        GUEST32(obj + 0x18) = bits & ~(0x08000000u | 0x04000000u);   /* not the stock line's own value */
        g_ecx = obj;
        sub_000AF580_gen(ctx, base);
        if (s_opt_view == OPT_V_BIND) { if (bind_lr(cur, dir)) opt_view_texts(OPT_V_BIND); }
        else if (rv_step(s_opt_view, (int)cur, dir)) {
            char buf[48];
            const opt_pl_t *spl = opt_pl(s_opt_view, (int)cur);
            rv_text(s_opt_view, (int)cur, buf, sizeof buf);
            if (spl && spl->kind == PL_VAL && spl->a == RV_ASPECT)
                opt_view_texts(s_opt_view);                   /* RENDER RESOLUTION lists the new aspect's sizes */
            else
                opt_line_text((int)cur, buf);                 /* SetText on its instances at the next call */
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
        else if ((s_opt_view == OPT_V_RECOMP || s_opt_view == OPT_V_SOUND) && rv_restart_pending()) {
            parent = OPT_V_RESTART; back_line = 1;            /* SOUND > SPEAKERS is restart-bound too */
        }
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
    if ((bits & 0x202u) && s_opt_view == OPT_V_TOP) {
        /* Leaving OPTIONS keeps every change (2026-11-05: AUF/AB and VIBRATION are only here now, maintainer). The
         * stock B CANCELS: it writes AUF/AB / VIBRATION / ANZEIGE, then copies the snapshot taken at the open
         * (obj+0x27C, 0xDA5 dwords) back over the whole settings block 0x342748.. - so it restores the old values;
         * only the stock VERLASSEN line kept them, and the maintainer's layout has no VERLASSEN (run
         * 20261105_020204_persist1: UMGEK. / AUS on screen, AUF/AB 0 / VIBRATION 1 after leaving). The live values go
         * into the snapshot first, so the restore keeps them (volumes, BRIGHTNESS / CONTRAST included). */
        uint32_t i;
        GUEST32(0x0034274Cu) = GUEST32(obj + 0x8C);
        GUEST32(0x00342750u) = GUEST32(obj + 0x90);
        GUEST32(0x00342790u) = GUEST32(obj + 0xA4);
        for (i = 0; i < 0xDA5u; i++) GUEST32(obj + 0x27C + 4u * i) = GUEST32(0x00342748u + 4u * i);
        fprintf(stderr, "[OPT] leaving OPTIONS: changes kept (AUF/AB %u, VIBRATION %u)\n", GUEST32(0x0034274Cu),
                GUEST32(0x00342750u));
    }
    g_ecx = obj;   /* opt_cursor_first's extra handler runs leave ecx changed (run 20261104_074744_opt_views1: ecx 0) */
    sub_000AF580_gen(ctx, base);
}

/* ── Main menu (maintainer 2026-11-04/05, docs/menu_rework.md) ─────────────────────────────────────────────────────
 *
 * Stock: NEUES SPIEL, WEITER, PANDORAS BÜCHSE, ÜBUNG, OPTIONEN (lines 0..4). Wanted: WEITER, NEUES SPIEL, PANDORAS
 * BÜCHSE, OPTIONEN, EXIT GAME - ÜBUNG moves into OPTIONS as PRACTICE, EXIT GAME asks YES / NO. Five rows either way,
 * so nothing moves on screen: each ROW keeps its sprites and gets the text and the action of another LINE.
 * The handler sub_000C0280 (this = the title screen object; static RE in docs/menu_rework.md): +0x10 state (0 open,
 * 1 fade in, 2 input, 3 confirm flash, 4 leave, 5 fade out), +0x2C input edge bits, +0xC0 cursor (= ROW here),
 * +0x30 "a save exists" (row WEITER is skipped without one), labels per row: normal 0x36E6D8[r] at +0x70+4r,
 * selected 0x36E6B0[r] at +0x98+4r (made on confirm), +0x84 / +0xAC freed on confirm; help id 0x36E6C4[line] through
 * sub_000BF630; result on leaving +0x20 = 0x36E69C[line], +0x1AC = 0x36E688[line].
 * Texts through the game's SetText (the sprite-create hook above, by record id, only while the menu is open - the
 * same ids exist in other sprite sets). The moved stock labels keep the game's own text in every language (decoded
 * from the loaded records); EXIT GAME, YES, NO are English in every language (maintainer). ORTA_MAIN_MENU=0 = stock. */
extern void sub_000C0280_gen(RECOMP_PARAMS);
extern void sub_00012610(RECOMP_PARAMS);
extern void sub_001BAB20(RECOMP_PARAMS);
extern void sub_000BF630(RECOMP_PARAMS);
extern void sub_0004F260(RECOMP_PARAMS);
extern void sub_0004F2C0(RECOMP_PARAMS);
extern void sub_000345B0(RECOMP_PARAMS);

static uint32_t opt_find_data(uint32_t id, uint32_t *node_out);
enum { MM_NEW, MM_CONT, MM_PB, MM_PRACTICE, MM_OPT, MM_EXIT };     /* stock lines 0..4, EXIT GAME = 5 */
#define MM_ROWS 5
static const int k_mm_line[MM_ROWS] = { MM_CONT, MM_NEW, MM_PB, MM_OPT, MM_EXIT };
static int s_mm_reg;            /* our texts registered with the create hook */
static int s_mm_confirm;        /* the EXIT GAME? YES / NO view is up */
static int s_mm_back_row = -1;  /* the row to land on when the menu comes back (from OPTIONEN / PANDORAS BÜCHSE) */
int g_orta_practice_request;    /* set by OPTIONS > PRACTICE: the main menu starts ÜBUNG as soon as it is up */
static char s_mm_text[MM_ROWS][48];

static int mm_on(void)
{
    const char *e = getenv("ORTA_MAIN_MENU");
    return !(e && *e == '0');
}

/* cdecl call of a guest function with up to 2 args; returns eax */
static uint32_t mm_cdecl(X86Ctx *ctx, uint8_t *base, void (*fn)(RECOMP_PARAMS), int n, uint32_t a0, uint32_t a1)
{
    if (n > 1) { g_esp -= 4; GUEST32(g_esp) = a1; }
    if (n > 0) { g_esp -= 4; GUEST32(g_esp) = a0; }
    g_esp -= 4; GUEST32(g_esp) = 0;                 /* return address slot, popped by the callee's ret */
    fn(ctx, base);
    g_esp += 4 * (uint32_t)n;
    return g_eax;
}

static void mm_help(X86Ctx *ctx, uint8_t *base, uint32_t obj, int line)
{
    /* sub_000BF630: thiscall, one arg, callee pops it (the stock call sites add nothing to esp after it) */
    /* EXIT GAME has no message of its own: -1 = no help text (the game's own call when the menu closes) */
    uint32_t id = line == MM_EXIT ? 0xFFFFFFFFu : GUEST32(0x0036E6C4u + 4u * (uint32_t)line);
    g_esp -= 4; GUEST32(g_esp) = id;
    g_esp -= 4; GUEST32(g_esp) = 0;
    g_ecx = obj;
    sub_000BF630(ctx, base);
}

/* the game's own text of a loaded record (its first text node), decoded; 0 when not found / not decodable */
static int mm_record_text(uint32_t id, char *out, size_t cap)
{
    uint32_t d = opt_find_data(id, NULL), k, node, c = 0, g = 0;
    if (!d || !(k = GUEST32(d + 0x54)) || !(node = GUEST32(k))) return 0;
    opt_node_stock(node, &c, &g);
    return opt_decode(g, c, out, cap);
}

/* Same row spacing on every row (maintainer 2026-11-05: "zeilenabstände immer gleich"). The rows' records carry their
 * place as x | y << 16 words (x 0x140 = the centre, 0xFED4 = the slide-in start) at y 70 / 120 / 170 / 220 / 280 - the
 * last row 60 below instead of 50. Evened out in the loaded records of all four row tables: y = 70 + 50 * row.
 * The letter size ("alle texte die selbe größe") is no record value: the menu font has small capitals on the LOWER
 * case codes, which every stock label uses; "EXIT GAME" in upper case got the big capitals (run 20261105_010125_
 * mm_live2: glyph codes 0x91.. against the labels' 0xAB..) - so the port's label is written in lower case. */
static void mm_even_rows(void)
{
    static const uint32_t k_tabs[4] = { 0x0036E6D8u, 0x0036E62Cu, 0x0036E6B0u, 0x0036E618u };
    static const uint32_t k_y[MM_ROWS] = { 0x46, 0x78, 0xAA, 0xDC, 0x118 };
    int t, r, i, done = 0;
    for (t = 0; t < 4; t++) {
        uint32_t pos_addr[MM_ROWS][4], npos[MM_ROWS] = { 0 };
        for (r = 0; r < MM_ROWS; r++) {
            uint32_t d = opt_find_data(GUEST32(k_tabs[t] + 4u * (uint32_t)r), NULL), blk[3], b;
            blk[0] = d;
            blk[1] = d ? GUEST32(d + 0x64) : 0;
            blk[2] = blk[1] ? GUEST32(blk[1] + 0x6C) : 0;
            for (b = 0; b < 3; b++) {
                if (blk[b] < 0x00010000u || blk[b] >= 0x04000000u) continue;
                for (i = 0; i < 40; i++) {
                    uint32_t a = blk[b] + 4u * (uint32_t)i, w = GUEST32(a);
                    if ((w >> 16) != k_y[r] || ((w & 0xFFFFu) != 0x140u && (w & 0xFFFFu) != 0xFED4u)) continue;
                    if (npos[r] < 4) pos_addr[r][npos[r]++] = a;
                }
            }
        }
        for (r = 0; r < MM_ROWS; r++) {
            uint32_t k;
            for (k = 0; k < npos[r]; k++) {
                uint32_t a = pos_addr[r][k];
                GUEST32(a) = (GUEST32(a) & 0xFFFFu) | ((0x46u + 50u * (uint32_t)r) << 16);
                done++;
            }
        }
    }
    fprintf(stderr, "[MENU] rows evened (%d position words)\n", done);
}

static void mm_register(void)
{
    int r;
    if (!getenv("ORTA_MM_KEEP_ROWS")) mm_even_rows();
    for (r = 0; r < MM_ROWS; r++) {
        int line = k_mm_line[r];
        if (line == MM_EXIT) snprintf(s_mm_text[r], sizeof s_mm_text[r], "exit game");   /* small capitals */
        else if (!mm_record_text(GUEST32(0x0036E6D8u + 4u * (uint32_t)line), s_mm_text[r], sizeof s_mm_text[r]))
            s_mm_text[r][0] = 0;
        fprintf(stderr, "[MENU] row %d = line %d \"%s\"\n", r, line, s_mm_text[r]);
    }
    for (r = 0; r < MM_ROWS; r++) {
        if (k_mm_line[r] == r || !s_mm_text[r][0]) continue;   /* PANDORAS BÜCHSE stays where it is */
        opt_settext_set(GUEST32(0x0036E6D8u + 4u * (uint32_t)r), s_mm_text[r]);
        opt_settext_set(GUEST32(0x0036E6B0u + 4u * (uint32_t)r), s_mm_text[r]);
        /* the cursor row's own sprites (sub_000BF900: 0x36E62C / 0x36E618 on the cursor row, 0x36E604 elsewhere) */
        opt_settext_set(GUEST32(0x0036E62Cu + 4u * (uint32_t)r), s_mm_text[r]);
        opt_settext_set(GUEST32(0x0036E618u + 4u * (uint32_t)r), s_mm_text[r]);
    }
    s_mm_reg = 1;
}

static void mm_unregister(void)
{
    int r;
    if (!s_mm_reg) return;
    for (r = 0; r < MM_ROWS; r++) {
        opt_settext_set(GUEST32(0x0036E6D8u + 4u * (uint32_t)r), NULL);
        opt_settext_set(GUEST32(0x0036E6B0u + 4u * (uint32_t)r), NULL);
        opt_settext_set(GUEST32(0x0036E62Cu + 4u * (uint32_t)r), NULL);
        opt_settext_set(GUEST32(0x0036E618u + 4u * (uint32_t)r), NULL);
    }
    s_mm_reg = 0;
}

static void mm_confirm_view(X86Ctx *ctx, uint8_t *base, uint32_t obj, int on)
{
    static const char *const k_q[MM_ROWS] = { "EXIT GAME?", "YES", "NO", " ", " " };
    static const uint32_t k_tabs[4] = { 0x0036E6D8u, 0x0036E6B0u, 0x0036E62Cu, 0x0036E618u };
    static const uint32_t k_live[4] = { 0x70, 0x84, 0x98, 0xAC };
    int r, t;
    s_mm_confirm = on;
    /* The game makes the row sprites again whenever the cursor moves (sub_000BF900), so the texts go into the create
     * hook's table (every row, all four record tables) AND onto the sprites alive now. */
    for (r = 0; r < MM_ROWS; r++) {
        const char *txt = on ? k_q[r] : s_mm_text[r];
        char stock[48];
        uint32_t b;
        if (!txt[0]) {
            if (!mm_record_text(GUEST32(0x0036E6D8u + 4u * (uint32_t)r), stock, sizeof stock)) continue;
            txt = stock;
        }
        for (t = 0; t < 4; t++) opt_settext_set(GUEST32(k_tabs[t] + 4u * (uint32_t)r), txt);
        b = opt_settext_buf(GUEST32(k_tabs[0] + 4u * (uint32_t)r));
        for (t = 0; b && t < 4; t++) {
            uint32_t inst = GUEST32(obj + k_live[t] + 4u * (uint32_t)r);
            if (inst) opt_settext(ctx, base, inst, b);
        }
    }
    GUEST32(obj + 0xC0) = on ? 2u : 4u;                  /* NO / back on EXIT GAME */
    mm_cdecl(ctx, base, sub_00012610, 2, on ? 9u : 7u, 0x4Bu);
    fprintf(stderr, "[MENU] EXIT GAME? %s\n", on ? "asked" : "no");
}

/* the stock confirm (state 2, A) for row r, with the line's own action - the game's code ties WEITER's load calls to
 * cursor 1, which is NEUES SPIEL's row now */
static void mm_confirm(X86Ctx *ctx, uint8_t *base, uint32_t obj, int r)
{
    static const uint32_t k_free[3] = { 0x98, 0x84, 0xAC };
    int line = k_mm_line[r], k;
    mm_cdecl(ctx, base, sub_00012610, 2, 9u, 0x4Bu);
    for (k = 0; k < 3; k++) {
        uint32_t a = obj + k_free[k] + 4u * (uint32_t)r, s = GUEST32(a);
        if (s) { mm_cdecl(ctx, base, sub_001BAB20, 1, s, 0); GUEST32(a) = 0; }
    }
    GUEST32(obj + 0x98 + 4u * (uint32_t)r) = mm_cdecl(ctx, base, sub_001BB610, 1, GUEST32(0x0036E6B0u + 4u * (uint32_t)r), 0);
    if (line == MM_CONT) {
        mm_cdecl(ctx, base, sub_0004F260, 2, 0u, GUEST32(0x0041A6E8u));
        mm_cdecl(ctx, base, sub_0004F2C0, 1, 1u, 0);
        mm_cdecl(ctx, base, sub_000345B0, 1, 1u, 0);
    }
    GUEST32(obj + 0x10) = 3;
    s_mm_back_row = (line == MM_OPT || line == MM_PB) ? r : -1;
    fprintf(stderr, "[MENU] row %d confirmed (line %d)\n", r, line);
}

void sub_000C0280(RECOMP_PARAMS)
{
    uint32_t obj = g_ecx, st, bits, st_after, ret;
    int r;
    if (!obj || !mm_on()) { sub_000C0280_gen(ctx, base); return; }
    {
        extern void orta_rumble_test_tick(void);
        orta_rumble_test_tick();
    }
    st = GUEST32(obj + 0x10);
    bits = GUEST32(obj + 0x2C);
    {
        static uint32_t last_st = 99, last_bits;
        if (st != last_st || (bits && bits != last_bits))
            fprintf(stderr, "[MENU] state %u bits 0x%08X cursor %u save %u\n", st, bits, GUEST32(obj + 0xC0),
                    GUEST32(obj + 0x30));
        last_st = st; last_bits = bits;
    }
    if (st == 0 && !s_mm_reg) mm_register();
    if (st == 2 && g_orta_practice_request) {
        /* OPTIONS > PRACTICE: ÜBUNG's result straight away (stock: +0x20 = 6, +0x1AC = -1, then leave) */
        g_orta_practice_request = 0;
        GUEST32(obj + 0x20) = GUEST32(0x0036E69Cu + 4u * MM_PRACTICE);
        GUEST32(obj + 0x1AC) = GUEST32(0x0036E688u + 4u * MM_PRACTICE);
        GUEST32(obj + 0x10) = 4;
        s_mm_back_row = 3;
        fprintf(stderr, "[MENU] PRACTICE from OPTIONS: starting ÜBUNG\n");
        g_eax = 0xFFFFFFFFu;
        g_esp += 4;                                      /* the handler's ret: pop the return address */
        return;
    }
    if (st == 2 && (bits & (0x101u | 0x202u | 0x01000000u | 0x02000000u))) {
        int row = (int)(GUEST32(obj + 0xC0) % MM_ROWS);
        GUEST32(obj + 0x2C) = 0;                         /* the stock handler sees no input this frame */
        g_ecx = obj;
        sub_000C0280_gen(ctx, base);
        GUEST32(obj + 0x2C) = bits;
        if (s_mm_confirm) {
            if (bits & 0x101u) {
                if (row == 1) {
                    extern void host_quit(void);
                    fprintf(stderr, "[MENU] EXIT GAME: yes\n");
                    host_quit();
                }
                mm_confirm_view(ctx, base, obj, 0);
            } else if (bits & 0x202u) {
                mm_confirm_view(ctx, base, obj, 0);
            } else {
                GUEST32(obj + 0xC0) = row == 1 ? 2u : 1u;
                mm_cdecl(ctx, base, sub_00012610, 2, 8u, 0x4Bu);
            }
        } else if (bits & 0x101u) {
            if (k_mm_line[row] == MM_EXIT) mm_confirm_view(ctx, base, obj, 1);
            else mm_confirm(ctx, base, obj, row);
        } else if (bits & 0x202u) {                      /* B: back to the title, as stock */
            mm_cdecl(ctx, base, sub_00012610, 2, 7u, 0x4Bu);
            GUEST32(obj + 0x20) = 3;
            GUEST32(obj + 0x10) = 4;
            s_mm_back_row = -1;
        } else {                                         /* up / down by row; WEITER's row only with a save */
            int dir = (bits & 0x02000000u) ? 1 : MM_ROWS - 1, k;
            mm_cdecl(ctx, base, sub_00012610, 2, 8u, 0x4Bu);
            for (k = 0; k < MM_ROWS; k++) {
                row = (row + dir) % MM_ROWS;
                if (!(k_mm_line[row] == MM_CONT && GUEST32(obj + 0x30) == 0)) break;
            }
            GUEST32(obj + 0xC0) = (uint32_t)row;
            mm_help(ctx, base, obj, k_mm_line[row]);
            fprintf(stderr, "[MENU] row %d\n", row);
        }
        g_eax = 0xFFFFFFFFu;
        return;
    }
    g_ecx = obj;
    sub_000C0280_gen(ctx, base);
    ret = g_eax;                 /* -1 while the menu runs; at the end of state 5 the result (+0x20) - kept */
    st_after = GUEST32(obj + 0x10);
    if (st == 0 && st_after != 0) {
        /* opened: the start row (back on OPTIONEN / PANDORAS BÜCHSE, else WEITER with a save, NEUES SPIEL without) */
        r = s_mm_back_row >= 0 ? s_mm_back_row : GUEST32(obj + 0x30) ? 0 : 1;
        if (k_mm_line[r] == MM_CONT && GUEST32(obj + 0x30) == 0) r = 1;
        GUEST32(obj + 0xC0) = (uint32_t)r;
        s_mm_confirm = 0;
        mm_help(ctx, base, obj, k_mm_line[r]);
        fprintf(stderr, "[MENU] open: row %d, save %s, AUF/AB %u, VIBRATION %u\n", r, GUEST32(obj + 0x30) ? "yes" : "no",
                GUEST32(0x0034274Cu), GUEST32(0x00342750u));
        if (getenv("ORTA_RUMBLE_TEST")) {             /* feel test: half power on both motors, the toolkit holds it */
            extern void orta_rumble_test(void);
            orta_rumble_test();
        }
    }
    if (st == 3 && st_after == 4) {                      /* the result by LINE, not by row */
        int line = k_mm_line[GUEST32(obj + 0xC0) % MM_ROWS];
        if (line <= MM_OPT) {
            GUEST32(obj + 0x20) = GUEST32(0x0036E69Cu + 4u * (uint32_t)line);
            GUEST32(obj + 0x1AC) = GUEST32(0x0036E688u + 4u * (uint32_t)line);
        }
    }
    if (st_after == 5 && s_mm_reg)
        fprintf(stderr, "[MENU] leaving: result %u / %d, labels %08X %08X %08X %08X %08X\n", GUEST32(obj + 0x20),
                (int)GUEST32(obj + 0x1AC), GUEST32(obj + 0x70), GUEST32(obj + 0x74), GUEST32(obj + 0x78),
                GUEST32(obj + 0x7C), GUEST32(obj + 0x80));
    if (st_after == 5) mm_unregister();
    g_eax = ret;
}

/* ── Rumble (maintainer 2026-11-05: "funktioniert eigentlich rumble?" - it did not) ───────────────────────────────
 * Orta's pad code: sub_0004FF80(slot, left, right) keeps the strongest request per pad slot in 0x449A90 + 8 * slot
 * (only while VIBRATION is EIN, 0x342750); the function at 0x0004F600 (edx = slot, ebx = the pad's device object) sends it through
 * the XPP driver when it changed against 0x449A70 - as a USB output report, which the toolkit's OHCI model accepts and
 * discards (ohci.c, OUT packets), so no motor ever turned. The override hands the same values to the toolkit's
 * xbox_InputSetState (SDL_RumbleGamepad) for the slot's pad (see below). ORTA_RUMBLE_TEST=1 (feel test): both motors
 * at half for 2 s whenever the main menu opens.
 * 2026-11-05 (maintainer's OK for the toolkit fix): the toolkit's OHCI model now forwards the output report itself
 * (toolkit usb_gamepad_output, RECOMP_USB_RUMBLE_TRACE=1 logs it), so the override only passes through - forwarding here
 * as well would set the motors twice. Removed at the relift of 2026-11-07. */
static void orta_rumble(uint32_t port, uint32_t left, uint32_t right)
{
    XBOX_VIBRATION v;
    DWORD rc;
    v.wLeftMotorSpeed = (WORD)(left > 0xFFFFu ? 0xFFFFu : left);
    v.wRightMotorSpeed = (WORD)(right > 0xFFFFu ? 0xFFFFu : right);
    rc = xbox_InputSetState(port, &v);
    fprintf(stderr, "[RUMBLE] port %u left %u right %u -> %s\n", port, (unsigned)v.wLeftMotorSpeed,
            (unsigned)v.wRightMotorSpeed, rc == 0 ? "sent to the controller" : "no controller on that port");
}

static uint64_t s_rumble_test_end;   /* ms (SDL_GetTicks) when the feel test stops, 0 = none */
uint64_t SDL_GetTicks(void);
void orta_rumble_test(void)
{
    uint32_t p;
    for (p = 0; p < 4; p++) orta_rumble(p, 0x8000u, 0x8000u);
    s_rumble_test_end = SDL_GetTicks() + 2000u;
}
void orta_rumble_test_tick(void)
{
    uint32_t p;
    if (s_rumble_test_end && SDL_GetTicks() >= s_rumble_test_end) {
        s_rumble_test_end = 0;
        for (p = 0; p < 4; p++) orta_rumble(p, 0, 0);
    }
}

/* The rumble function at 0x0004F600: its pass-through override is gone with the relift of 2026-11-07 (the toolkit
 * forwards the report; the lifter takes any "sub_<address>" name in this file as an override, so it is not named).
 * History: the forward used the SLOT as the toolkit pad, not the XPP port number in 0x449D68 - the toolkit plugs its
 * pads in order (pad 0 on HC0 port 1), which Orta's XPP calls port 2 (maintainer's session 2026-11-05 02:45). */

/* ── Pause menu in gameplay (maintainer 2026-11-04/05, docs/menu_rework.md) ───────────────────────────────────────
 *
 * Stock (sub_001B4BA0, this = the pause object; static RE): +0x134 state (6 = input), +0x138 cursor = LINE 0..last
 * (last = [+0x178] ? 4 : 5), lines WEITER, ERNEUT VERS., AUF/AB (+ value NORMAL / INVERS), VIBRATION (+ EIN / AUS),
 * SPEICH. & ENDE, SPIEL BEENDEN; record per line and value: idx = line + 6 * value, normal 0x3CF900[idx] (+0x1C +
 * 4*line), selected 0x3CF960[idx] (+0x34), bar 0x3CF930[idx] (+0x4C); A on lines 2 / 3 toggles 0x34274C / 0x342750.
 * Wanted: AUF/AB and VIBRATION out (they stay in OPTIONS), EXIT GAME right before SPEICH. & ENDE. */
extern void sub_001B4BA0_gen(RECOMP_PARAMS);

static void pm_dump_words(uint32_t p, int depth, uint32_t idtag)
{
    int i;
    if (depth > 6 || p < 0x00400000u || p >= 0x04000000u || !opt_set_add(0, p, depth)) return;
    for (i = 0; i < 64; i++) {
        uint32_t a = p + 4u * (uint32_t)i, v = GUEST32(a), x = v & 0xFFFFu, y = v >> 16;
        if (((x > 0 && x < 640) || x == 0xFED4u) && y >= 22 && y < 480)
            fprintf(stderr, "[PMREC]   id 0x%X depth %d @%08X x %u y %u\n", idtag, depth, a, x, y);
        else if (v >= 0x00400000u && v < 0x04000000u)
            pm_dump_words(v, depth + 1, idtag);
    }
}

/* The port's pause layout: line 2 (AUF/AB) becomes EXIT GAME - its text through the game's per-language tables (every
 * pointer to the AUF/AB string, read by sub_001B3670 at each create), its value box hidden; line 3 (VIBRATION) hidden
 * and skipped; lines 4 / 5 (SPEICH. & ENDE, SPIEL BEENDEN) three rows up, so the rows stay 30 px apart. EXIT GAME asks
 * in its own line: A -> "exit game? a yes  b no", A again quits, B / up / down cancel. The input edge bits are the
 * global [0x47DF2C] (sub_001B1CB0), so they are filtered around the stock handler. ORTA_PAUSE_MENU=0 = stock. */
#define PM_INPUT 0x0047DF2Cu
static int s_pm_confirm;
static uint32_t s_pm_str_exit, s_pm_str_ask;

static uint32_t pm_guest_str(const char *t)
{
    uint32_t b = xbox_HeapAlloc(48, 16), j, n = (uint32_t)strlen(t);
    if (!b) return 0;
    for (j = 0; j <= n && j < 47; j++) *(volatile uint8_t *)((uintptr_t)g_xbox_mem_offset + b + j) = (uint8_t)t[j];
    return b;
}

/* once: the AUF/AB entries of every language table -> "exit game" (lower case = the menu font's small capitals) */
static void pm_patch_texts(void)
{
    static int done;
    static const char *const k_updown[] = { "up/down", "auf/ab", "ht/bas", "ar./ab.", "su/gi" };
    uint32_t a;
    int n = 0;
    if (done) return;
    done = 1;
    s_pm_str_exit = pm_guest_str("exit game");
    s_pm_str_ask = pm_guest_str("exit game? a yes b no");   /* must end left of the wing panels (x ~345) */
    if (!s_pm_str_exit) return;
    for (a = 0x003CC200u; a < 0x003CDE00u; a += 4) {
        uint32_t p = GUEST32(a), k;
        char s[12];
        if (p < 0x00300000u || p >= 0x00400000u) continue;
        for (k = 0; k < 11; k++) s[k] = (char)*(volatile uint8_t *)((uintptr_t)g_xbox_mem_offset + p + k);
        s[11] = 0;
        for (k = 0; k < sizeof k_updown / sizeof k_updown[0]; k++)
            if (!strncmp(s, k_updown[k], strlen(k_updown[k])) && (k == 4 || s[strlen(k_updown[k])] == 0)) {
                GUEST32(a) = s_pm_str_exit;
                n++;
                break;
            }
    }
    fprintf(stderr, "[PAUSE] AUF/AB text entries -> \"exit game\": %d\n", n);
}

/* alpha [[s+0x50]+8] of sprite s, its children (+0x14) and their siblings (+0x0C), recursively */
static void pm_alpha_tree(uint32_t s, uint32_t bits, int depth)
{
    uint32_t n;
    for (n = 0; s >= 0x00010000u && s < 0x04000000u && n < 16 && depth < 6; s = GUEST32(s + 0x0C), n++) {
        uint32_t t = GUEST32(s + 0x50);
        if (t >= 0x00010000u && t < 0x04000000u) GUEST32(t + 8) = bits;
        pm_alpha_tree(GUEST32(s + 0x14), bits, depth + 1);
    }
}

/* every sprite of a created container (the first one [inst+0x24], its +0x0C siblings and all children); keep_first:
 * the label itself stays, only what hangs below it goes - the value box (NORMAL / INVERS, EIN / AUS text and its
 * frame) is a CHILD of the label at +0x14 (run 20261105_013554_preplay6: S+0x14 set only on lines 2 and 3) */
static void pm_alpha(uint32_t inst, int keep_first, float a)
{
    uint32_t s = inst ? GUEST32(inst + 0x24) : 0, bits;
    memcpy(&bits, &a, 4);
    if (!(s >= 0x00010000u && s < 0x04000000u)) return;
    if (keep_first) {
        pm_alpha_tree(GUEST32(s + 0x14), bits, 1);
        pm_alpha_tree(GUEST32(s + 0x0C), bits, 1);
    } else {
        pm_alpha_tree(s, bits, 0);
    }
}

/* lines 4 / 5 three rows up: their records' x | y << 16 words in the row band (y 240 / 270 -> 150 / 180), once per
 * loaded record (the pause set is loaded per level) */
/* One record's position words with y in [lo, hi] -> y + dy: its key P = [[D+0x64]+0x6C] (2 words), the key block K =
 * [D+0x64] itself (inline places of the selected / bar records), then its child record [D+0x14] the same way (the
 * value box under AUF/AB, the bar's line and sparkles). Only words whose x is a place (0xFFF3 / 0xFEAC, the pause's
 * offsets, or 0..639) count. A blind walk through every pointer moved non-position words and crashed (run
 * 20261105_014510_preplay11). */
static int pm_shift_rec(uint32_t d, uint32_t lo, uint32_t hi, int dy, int depth)
{
    uint32_t k, p, j;
    int n = 0;
    if (depth > 2 || d < 0x00400000u || d >= 0x04000000u) return 0;
    k = GUEST32(d + 0x64);
    if (k >= 0x00400000u && k < 0x04000000u) {
        p = GUEST32(k + 0x6C);
        for (j = 0; j < 32 + 2; j++) {
            uint32_t a = j < 32 ? k + 4u * j : p + 4u * (j - 32), w, x, y;
            if (j >= 32 && !(p >= 0x00400000u && p < 0x04000000u)) break;
            w = GUEST32(a); x = w & 0xFFFFu; y = w >> 16;
            if ((x == 0xFFF3u || x == 0xFEACu || x < 640u || (dy > 300 && x >= 0xFE00u)) && y >= lo && y <= hi &&
                opt_set_add(1, a, 0)) {
                GUEST32(a) = x | ((uint32_t)((int)y + dy) << 16);
                n++;
            }
        }
    }
    n += pm_shift_rec(GUEST32(d + 0x14), lo, hi, dy, depth + 1);
    return n;
}

static void pm_move_rows(void)
{
    /* 0x3CFBC0: the lines made at the open (state 3); 0x3CF900 / 960 / 930: remade on every cursor move */
    /* 0x3CFB00 (6 entries, by line only): the line confirmed with A while its dialog is up (sub_001B4B60) - SPIEL
     * BEENDEN stayed in its old row there (maintainer's screenshot 2026-11-05 ~02:05) */
    static const uint32_t k_t[5] = { 0x003CFBC0u, 0x003CF900u, 0x003CF960u, 0x003CF930u, 0x003CFB00u };
    uint32_t seen[64];
    int nseen = 0;                 /* a moved word leaves the band, so a second call moves nothing twice */
    int t, idx, moved = 0, parked = 0;
    /* Positions (runs 20261105_013713_preplay7, _014116_preplay9): a line record's place is x | y << 16 - in its key
     * P = [[D+0x64]+0x6C] (x 0xFEAC slide-in start, 0xFFF3 place; y 90 / 120 / 180 / 240 / 270 for lines 1..5), the
     * selected / bar records inline in their K block, the bar's sparkles in its own sub-blocks. Lines 4 / 5: 90 up.
     * Line 2 (now EXIT GAME): its value frame (y ~150, drawn by the label sprite itself, so alpha cannot hide it) far
     * below the screen; the label at y 120 stays. */
    for (t = 0; t < 5; t++)
        for (idx = 0; idx < (t == 4 ? 6 : 12); idx++) {
            int line = idx % 6, k, dup = 0;
            uint32_t d;
            if (line != 2 && line < 4) continue;
            d = opt_find_data(GUEST32(k_t[t] + 4u * (uint32_t)idx), NULL);
            if (!d) continue;
            for (k = 0; k < nseen; k++) if (seen[k] == d) dup = 1;
            if (dup) continue;
            if (nseen < 64) seen[nseen++] = d;
            if (++s_gen == 0) s_gen = 1;
            /* Child records place themselves RELATIVE to their parent (run 20261105_014908_preplay13: the value frame
             * 0x00140044 = 68 / 20 below the label), so moving a parent takes its children along and the frame is
             * parked by its own small offset. Bands per line: the bar sits 23 above its row (line 4: 217, line 5: 247
             * - inside line 4's label band, so each line's records get their own band). */
            /* depth 2 = this record only (its children follow it) */
            if (line == 2 && t != 3) {
                /* the value text and the box drawn under it: the child [D+0x14] and its siblings (+0x0C; the
                 * selected record's box is a sibling, run _015359_preplay16: 012FA570 -> 012FA6A8) */
                uint32_t s, guard;
                for (s = GUEST32(d + 0x14), guard = 0; s >= 0x00400000u && s < 0x04000000u && guard < 8;
                     s = GUEST32(s + 0x0C), guard++)
                    parked += pm_shift_rec(s, 10u, 40u, 600, 2);
            } else if (line == 2) {
                /* the bar record (0x3CF930): its FIRST child is the sparkle set every bar shares (moving it moved
                 * the sparkles of all lines, run 20261105_015359_preplay16) - only the siblings after it, the
                 * turquoise value box the bar lights up under AUF/AB, are parked */
                uint32_t c = GUEST32(d + 0x14), s, guard;
                for (s = c >= 0x00400000u && c < 0x04000000u ? GUEST32(c + 0x0C) : 0, guard = 0;
                     s >= 0x00400000u && s < 0x04000000u && guard < 8; s = GUEST32(s + 0x0C), guard++)
                    parked += pm_shift_rec(s, 10u, 40u, 600, 2);
            }
            else if (line == 4) moved += pm_shift_rec(d, 200u, 252u, -90, 2);
            else moved += pm_shift_rec(d, 240u, 300u, -90, 2);
        }
    if (moved || parked) fprintf(stderr, "[PAUSE] rows 4/5 up: %d words, EXIT GAME's value frame parked: %d words\n",
                                 moved, parked);
    if (getenv("ORTA_DIAG_PAUSE")) {
        static const struct { uint32_t tab; int idx; } k_w[2] = { { 0x003CF960u, 2 }, { 0x003CF930u, 2 } };
        int w2;
        for (w2 = 0; w2 < 2; w2++) {
            uint32_t d = opt_find_data(GUEST32(k_w[w2].tab + 4u * (uint32_t)k_w[w2].idx), NULL), c, lvl, i;
            for (c = d, lvl = 0; c >= 0x00400000u && c < 0x04000000u && lvl < 3; c = GUEST32(c + 0x14), lvl++) {
                uint32_t k = GUEST32(c + 0x64);
                fprintf(stderr, "[PMCHILD] %d lvl %u D %08X:", w2, lvl, c);
                for (i = 0; i < 40; i++) fprintf(stderr, " %08X", GUEST32(c + 4 * i));
                fprintf(stderr, "\n  K %08X:", k);
                for (i = 0; k >= 0x00400000u && k < 0x04000000u && i < 40; i++) fprintf(stderr, " %08X", GUEST32(k + 4 * i));
                fprintf(stderr, "\n");
            }
        }
    }
}

static void pm_hide(uint32_t obj)
{
    static const uint32_t k_slot[3] = { 0x1C, 0x34, 0x4C };
    int k;
    for (k = 0; k < 3; k++) {
        pm_alpha(GUEST32(obj + k_slot[k] + 4u * 2u), 1, 0.0f);   /* EXIT GAME: the value box under it */
        pm_alpha(GUEST32(obj + k_slot[k] + 4u * 3u), 0, 0.0f);   /* VIBRATION: all of it */
    }
}

/* EXIT GAME asks with the game's own dialog, as SPIEL BEENDEN does (maintainer 2026-11-05 ~02:05): sub_001C2490(msg, 0)
 * opens it, sub_001C24C0() == -2 = answered, sub_001C24F0() = the answer (0 = JA, as the stock flow reads it),
 * sub_001C2490(-1, 0) closes it (stock states 8 / 9). The box takes its text from the message table at the open
 * (entry = sub_001AFE00(id), ecx = [0x47DF20]; string = [[entry+0xC]+0xC]): the text of message 0x10 (SPIEL BEENDEN's
 * "back to the title without saving?") is swapped for ours just around the open. English in every language. */
extern void sub_001AFE00(RECOMP_PARAMS);
extern void sub_001C2490(RECOMP_PARAMS);
extern void sub_001C24C0(RECOMP_PARAMS);
extern void sub_001C24F0(RECOMP_PARAMS);
static uint32_t s_pm_str_dialog;

extern void sub_001B4B60(RECOMP_PARAMS);

/* The message entries of id 0x10 point at our text (on) or back at the game's (off). */
static void pm_dialog_text(int on)
{
    static uint32_t old[6], ent[6];
    static int swapped;
    const uint32_t id = 0x10u;
    uint32_t k;
    if (!ent[0]) {
        ent[0] = 0x003D5E98u + 12u * id; ent[1] = 0x003D5FB8u + 12u * id; ent[2] = 0x003D60D8u + 12u * id;
        ent[3] = 0x003D61F8u + 12u * id; ent[4] = 0x003D5C58u + 12u * (4u + 2u * id); ent[5] = 0x003D5C64u + 24u * id;
    }
    (void)s_pm_str_dialog;
    if (on && !swapped) {
        /* The stock text ends with its choices: "...\n\n%s0Ja%s0         %s1Nein%s1" (the box draws %s0 / %s1 as the
         * two answers). Ours = the English question + that language's own choices, built once per entry. */
        static uint32_t mine[6];
        for (k = 0; k < 6; k++) {
            old[k] = GUEST32(ent[k] + 8);
            if (!(old[k] >= 0x00300000u && old[k] < 0x00400000u)) { old[k] = 0; continue; }
            if (!mine[k]) {
                char orig[160], txt[200];
                const char *ch;
                uint32_t j;
                for (j = 0; j < sizeof orig - 1; j++) {
                    orig[j] = (char)*(volatile uint8_t *)((uintptr_t)g_xbox_mem_offset + old[k] + j);
                    if (!orig[j]) break;
                }
                orig[sizeof orig - 1] = 0;
                ch = strstr(orig, "%s0");
                snprintf(txt, sizeof txt, "Exit the game?\nUnsaved progress is lost.\n\n%s", ch ? ch : "");
                mine[k] = xbox_HeapAlloc(200, 16);
                if (mine[k]) {
                    for (j = 0; j <= strlen(txt) && j < 199; j++)
                        *(volatile uint8_t *)((uintptr_t)g_xbox_mem_offset + mine[k] + j) = (uint8_t)txt[j];
                }
            }
            if (mine[k]) GUEST32(ent[k] + 8) = mine[k];
            else old[k] = 0;
        }
        swapped = 1;
    } else if (!on && swapped) {
        for (k = 0; k < 6; k++) if (old[k]) GUEST32(ent[k] + 8) = old[k];
        swapped = 0;
    }
}

/* A on EXIT GAME: what the stock does for a line with a dialog (0x1B4FC2): sound, the line's bar and selected sprites
 * freed, sub_001B4B60(line) (the confirmed sprite), state 7 - whose end opens the dialog 0x3CFB48[line + 6 * variant]
 * (patched to message 0x10 for line 2). The answer is read in state 8 before the stock acts on it. */
static void pm_start_confirm(X86Ctx *ctx, uint8_t *base, uint32_t obj)
{
    uint32_t k;
    GUEST32(0x003CFB48u + 4u * 2u) = 0x10u;
    GUEST32(0x003CFB48u + 4u * 8u) = 0x10u;
    mm_cdecl(ctx, base, sub_00012610, 2, 9u, 0x4Bu);
    for (k = 0; k < 2; k++) {
        uint32_t a = obj + (k ? 0x34u : 0x4Cu) + 4u * 2u, s = GUEST32(a);
        if (s) { mm_cdecl(ctx, base, sub_001BAB20, 1, s, 0); GUEST32(a) = 0; }
    }
    /* sub_001B4B60(line) makes the "confirmed" sprite from 0x3CFB00[line] into +0x80 and sets its text
     * (sub_001B3670(inst, line)) - line 2 has none there (0: the create returned 0 and the text call crashed, run
     * 20261105_021821_pexit4), so the same with line 2's SELECTED record (0x3CF960[2 + 6 * value]: its place is row 2
     * and its value frame is parked) */
    if (!GUEST32(obj + 0x80)) {
        uint32_t rec = GUEST32(0x003CF960u + 4u * (GUEST32(0x0034274Cu) ? 8u : 2u)), inst;
        inst = mm_cdecl(ctx, base, sub_001BB610, 1, rec, 0);
        GUEST32(obj + 0x80) = inst;
        if (inst) {
            extern void sub_001B3670(RECOMP_PARAMS);
            opt_guest_call(ctx, base, sub_001B3670, obj, 2, inst, 2u);   /* thiscall, the callee pops its args */
        }
    }
    GUEST32(obj + 0x134) = 7;
    s_pm_confirm = 1;
    fprintf(stderr, "[PAUSE] EXIT GAME? asked\n");
}

/* State 8 with our dialog up: answered (sub_001C24C0() == -2) with JA (sub_001C24F0() == 0, as the stock reads it)
 * -> quit before the stock acts; NEIN -> the stock's own way back to the menu. */
static void pm_dialog_answer(X86Ctx *ctx, uint8_t *base)
{
    uint32_t r = mm_cdecl(ctx, base, sub_001C24C0, 0, 0, 0), ans;
    if (r != 0xFFFFFFFEu) return;                         /* not answered yet */
    ans = mm_cdecl(ctx, base, sub_001C24F0, 0, 0, 0);
    fprintf(stderr, "[PAUSE] EXIT GAME: %s\n", ans == 0 ? "yes" : "no");
    if (ans == 0) {
        extern void host_quit(void);
        host_quit();
    }
    s_pm_confirm = 0;
}

static int pm_on(void)
{
    const char *e = getenv("ORTA_PAUSE_MENU");
    return !(e && *e == '0');
}

void sub_001B4BA0(RECOMP_PARAMS)
{
    uint32_t obj = g_ecx;
    static uint32_t last_st = 99;
    uint32_t st = obj ? GUEST32(obj + 0x134) : 0;
    if (obj && pm_on()) {
        uint32_t in = GUEST32(PM_INPUT), keep = in, cur = GUEST32(obj + 0x138), ret;
        pm_patch_texts();
        if (st == 0) pm_move_rows();   /* before the open states create the line sprites */
        if (st == 6) {
            s_pm_confirm = 0;
            if ((in & 0x02000000u) && cur == 2) GUEST32(obj + 0x138) = 3;      /* down over VIBRATION */
            if ((in & 0x01000000u) && cur == 4) GUEST32(obj + 0x138) = 3;      /* up over VIBRATION */
            if ((in & 0x0C000000u) && (cur == 2 || cur == 3)) in &= ~0x0C000000u;   /* no AUF/AB / VIBRATION toggles */
            if ((in & 0x101u) && cur == 2) {
                in = 0;                                   /* not the stock AUF/AB toggle */
                pm_start_confirm(ctx, base, obj);         /* -> state 7, the stock's dialog way */
            }
        }
        if (s_pm_confirm && GUEST32(obj + 0x134) == 7) pm_dialog_text(1);   /* the open at the end of state 7 */
        if (s_pm_confirm && st == 8) pm_dialog_answer(ctx, base);
        GUEST32(PM_INPUT) = in;
        g_ecx = obj;
        sub_001B4BA0_gen(ctx, base);
        ret = g_eax;
        GUEST32(PM_INPUT) = keep;
        if (GUEST32(obj + 0x134) != 7) pm_dialog_text(0);
        pm_hide(obj);
        if (st != last_st) {
            fprintf(stderr, "[PAUSE] state %u cursor %u last-line flag %u\n", st, GUEST32(obj + 0x138), GUEST32(obj + 0x178));
            last_st = st;
        }
        g_eax = ret;
        return;
    }
    if (obj && st != last_st) {
        fprintf(stderr, "[PAUSE] state %u cursor %u last-line flag %u\n", st, GUEST32(obj + 0x138), GUEST32(obj + 0x178));
        if (st == 6 && getenv("ORTA_DIAG_PAUSE")) {
            static const uint32_t k_t[3] = { 0x003CF900u, 0x003CF960u, 0x003CF930u };
            int t, idx;
            for (t = 0; t < 3; t++)
                for (idx = 0; idx < 12; idx++) {
                    uint32_t id = GUEST32(k_t[t] + 4u * (uint32_t)idx), d = opt_find_data(id, NULL);
                    fprintf(stderr, "[PMREC] table %d idx %d id 0x%X D %08X\n", t, idx, id, d);
                    if (++s_gen == 0) s_gen = 1;
                    if (d) pm_dump_words(d, 0, id);
                }
        }
        last_st = st;
    }
    g_ecx = obj;
    sub_001B4BA0_gen(ctx, base);
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
