/*
 * apu_diag_win32.c - dev diagnostics for the LLE audio path (M1/M2 bring-up, not in releases).
 *
 * ORTA_APU_DIAG=<ms> (e.g. 1000): a host thread prints a snapshot of the emulated MCPX APU every <ms>:
 * interrupt status/enable, front-end/setup-engine control, the voice-list heads (2D, 3D, multipass),
 * and every voice the voice processor has ever processed (the toolkit marks g_dbg.vp.v[v].active when
 * it runs a voice; nothing clears it) with its buffer offsets, so one can see whether a voice starts and
 * whether its current offset (cbo) moves. Reads only: register reads go through mcpx_apu_mmio_read, which
 * has no side effects for these offsets (XGSCNT is computed). Written to stderr with a "[APUDIAG]" tag.
 *
 * Why (2026-11-01, run 20261101_130201_m2_ng_wd): after START at the title the loader thread waits on the
 * sound library's stream service and the APU delivered 0 interrupts in 75 s (Burnout 3 PAL on the same
 * toolkit path: 99) - find out whether any voice runs at all.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <windows.h>

#include "apu.h"
#include "apu_debug.h"

extern MCPXAPUState *g_apu_state;
extern struct McpxApuDebug g_dbg;

static const struct { uint32_t off; const char *name; } k_regs[] = {
    {0x1000, "ISTS"}, {0x1004, "IEN"},   {0x1100, "FECTL"}, {0x1110, "FECV"},  {0x1118, "FEAV"},
    {0x1300, "FEDECMETH"}, {0x1304, "FEDECPARAM"}, {0x1324, "FEMEMADDR"}, {0x1334, "FEMEMDATA"},
    {0x2000, "SECTL"}, {0x202C, "VPVADDR"}, {0x2030, "VPSGEADDR"}, {0x2034, "VPSSLADDR"},
    {0x2040, "GPSADDR"},
    {0x2054, "TVL2D"}, {0x2058, "CVL2D"}, {0x205C, "NVL2D"},
    {0x2060, "TVL3D"}, {0x2064, "CVL3D"}, {0x2068, "NVL3D"},
    {0x206C, "TVLMP"}, {0x2070, "CVLMP"}, {0x2074, "NVLMP"},
};

/* ORTA_DIAG_PEEK=<va>[:<dwords>],... - guest memory printed with every snapshot (default 8 dwords). */
static void peek_guest(unsigned tick)
{
    extern ptrdiff_t g_xbox_mem_offset;
    const char *spec = getenv("ORTA_DIAG_PEEK");
    char buf[512], *tok, *save = NULL;
    if (!spec || !*spec || !g_xbox_mem_offset)
        return;
    strncpy(buf, spec, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    for (tok = strtok_s(buf, ",", &save); tok; tok = strtok_s(NULL, ",", &save)) {
        char *colon;
        uint32_t va = (uint32_t)strtoul(tok, &colon, 0);
        unsigned n = (*colon == ':') ? (unsigned)strtoul(colon + 1, NULL, 0) : 8, k;
        if (n > 64)
            n = 64;
        fprintf(stderr, "[PEEK] t=%u %08X:", tick, va);
        for (k = 0; k < n; k++)
            fprintf(stderr, "%s%08X", (k && k % 8 == 0) ? "\n[PEEK]            " : " ",
                    *(volatile uint32_t *)((uintptr_t)g_xbox_mem_offset + va + 4 * k));
        fprintf(stderr, "\n");
    }
}

static DWORD WINAPI apu_diag_thread(LPVOID arg)
{
    DWORD period = (DWORD)(uintptr_t)arg;
    unsigned tick = 0;
    for (;;) {
        Sleep(period);
        tick++;
        peek_guest(tick);
        if (!g_apu_state)
            continue;
        {
            char line[1024];
            int n = snprintf(line, sizeof line, "[APUDIAG] t=%u", tick);
            size_t i;
            for (i = 0; i < sizeof k_regs / sizeof k_regs[0] && n < (int)sizeof line - 32; i++)
                n += snprintf(line + n, sizeof line - n, " %s=%08X", k_regs[i].name,
                              (uint32_t)mcpx_apu_mmio_read(g_apu_state, k_regs[i].off, 4));
            fprintf(stderr, "%s\n", line);
        }
        {
            int v, shown = 0, active = 0;
            for (v = 0; v < 256; v++)
                if (g_dbg.vp.v[v].active)
                    active++;
            fprintf(stderr, "[APUDIAG]   voices ever processed: %d\n", active);
            for (v = 0; v < 256 && shown < 12; v++) {
                const struct McpxApuDebugVoice *dv = &g_dbg.vp.v[v];
                if (!dv->active)
                    continue;
                shown++;
                fprintf(stderr, "[APUDIAG]   v%-3d %s%s%s ba=%08X cbo=%08X lbo=%08X ebo=%08X rate=%.0f bins=%u,%u\n",
                        v, dv->paused ? "P" : "-", dv->stream ? "S" : "-", dv->loop ? "L" : "-",
                        dv->ba, dv->cbo, dv->lbo, dv->ebo, dv->rate, dv->bin[0], dv->bin[1]);
            }
        }
        fflush(stderr);
    }
    return 0;
}

void orta_apu_diag_start(void)
{
    const char *e = getenv("ORTA_APU_DIAG");
    long ms = e ? strtol(e, NULL, 0) : 0;
    if (ms <= 0)
        return;
    if (ms < 100)
        ms = 100;
    CreateThread(NULL, 0, apu_diag_thread, (LPVOID)(uintptr_t)ms, 0, NULL);
    fprintf(stderr, "[APUDIAG] snapshot every %ld ms\n", ms);
}
