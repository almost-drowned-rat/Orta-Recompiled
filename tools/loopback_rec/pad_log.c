/* pad_log.c - dev tool (never shipped): logs the state changes of the first gamepad (read only) with
 * millisecond stamps, so a maintainer session in xemu can later be turned into a pad script for the port.
 *
 *   pad_log.exe <out.txt>       runs until the file <out.txt>.stop exists
 *
 * SDL3 gamepad API (any pad SDL knows, e.g. the maintainer's DualSense, which is not XInput). Read only:
 * HIDAPI is off (SDL_HINT_JOYSTICK_HIDAPI=0), so no output reports (lights / rumble) are ever sent and the game
 * (xemu, which opens the pad itself) is not disturbed; the pad is read via DirectInput / RawInput instead.
 * Background events on (the game window has the focus). Polls every 4 ms (SDL_Delay, no busy wait).
 * One line per change, Xbox names (SDL's south = A, east = B, west = X, north = Y):
 *   <ms since start> <buttons, '+'-joined or -> lt=<0..32767> rt=<0..32767> lx=<int> ly=<int> rx=<int> ry=<int>
 * Line 1 is a header with the wall clock start time, to align the log with video/audio (times.txt).
 * Build: clang -O2 -o pad_log.exe pad_log.c -I<deps>\include -L<deps>\lib -lSDL3 (SDL3.dll next to the exe).
 * Origin: written for this port (2026-11-03), no third-party code. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static const struct { SDL_GamepadButton b; const char *name; } k_buttons[] = {
    { SDL_GAMEPAD_BUTTON_SOUTH, "a" }, { SDL_GAMEPAD_BUTTON_EAST, "b" }, { SDL_GAMEPAD_BUTTON_WEST, "x" },
    { SDL_GAMEPAD_BUTTON_NORTH, "y" }, { SDL_GAMEPAD_BUTTON_START, "start" }, { SDL_GAMEPAD_BUTTON_BACK, "back" },
    { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "lb" }, { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, "rb" },
    { SDL_GAMEPAD_BUTTON_LEFT_STICK, "ls" }, { SDL_GAMEPAD_BUTTON_RIGHT_STICK, "rs" },
    { SDL_GAMEPAD_BUTTON_DPAD_UP, "up" }, { SDL_GAMEPAD_BUTTON_DPAD_DOWN, "down" },
    { SDL_GAMEPAD_BUTTON_DPAD_LEFT, "left" }, { SDL_GAMEPAD_BUTTON_DPAD_RIGHT, "right" },
};
#define NB (int)(sizeof k_buttons / sizeof k_buttons[0])

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: pad_log <out.txt>\n"); return 2; }
    char stop_path[MAX_PATH];
    snprintf(stop_path, sizeof stop_path, "%s.stop", argv[1]);
    DeleteFileA(stop_path);
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_GAMEPAD)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    FILE *f = fopen(argv[1], "w");
    if (!f) { fprintf(stderr, "cannot write %s\n", argv[1]); return 1; }
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "# pad_log SDL3 gamepad (HIDAPI off), start %02u:%02u:%02u.%03u, poll 4 ms\n",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    fflush(f);
    Uint64 t0 = SDL_GetTicks();
    SDL_Gamepad *pad = NULL;
    char last[512] = "";
    unsigned polls = 0;
    for (;;) {
        SDL_Delay(4);
        if ((++polls & 63) == 0 && GetFileAttributesA(stop_path) != INVALID_FILE_ATTRIBUTES) break;
        SDL_UpdateGamepads();
        Uint64 ms = SDL_GetTicks() - t0;
        if (!pad) {
            int n = 0;
            SDL_JoystickID *ids = SDL_GetGamepads(&n);
            if (ids && n > 0) {
                pad = SDL_OpenGamepad(ids[0]);
                if (pad) { fprintf(f, "%llu connected %s\n", (unsigned long long)ms, SDL_GetGamepadName(pad)); fflush(f); }
            }
            SDL_free(ids);
            if (!pad) continue;
        }
        if (!SDL_GamepadConnected(pad)) {
            fprintf(f, "%llu disconnected\n", (unsigned long long)ms); fflush(f);
            SDL_CloseGamepad(pad); pad = NULL; last[0] = 0; continue;
        }
        char line[512]; int len = 0, any = 0;
        for (int i = 0; i < NB; i++)
            if (SDL_GetGamepadButton(pad, k_buttons[i].b)) {
                len += snprintf(line + len, sizeof line - len, "%s%s", any ? "+" : "", k_buttons[i].name); any = 1;
            }
        if (!any) len += snprintf(line + len, sizeof line - len, "-");
        snprintf(line + len, sizeof line - len, " lt=%d rt=%d lx=%d ly=%d rx=%d ry=%d",
                 SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER), SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER),
                 SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX), SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY),
                 SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX), SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY));
        if (strcmp(line, last) != 0) {
            fprintf(f, "%llu %s\n", (unsigned long long)ms, line);
            strcpy(last, line);
        }
        if ((polls & 255) == 0) fflush(f);
    }
    fclose(f);
    if (pad) SDL_CloseGamepad(pad);
    SDL_Quit();
    return 0;
}
