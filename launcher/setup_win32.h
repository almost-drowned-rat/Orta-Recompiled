/* setup_win32.h -- Orta's setup window in a Windows 95 look (setup_win32.c). */
#ifndef ORTA_SETUP_WIN32_H
#define ORTA_SETUP_WIN32_H

#ifdef __cplusplus
extern "C" {
#endif

/* The setup window (disc image, install folder, Go / Play); returns the exit code. Checks Vulkan 1.3 first. */
int orta_setup_window(void);
/* 1 when a Vulkan 1.3 device is there, else the apology box (not with RECOMP_NO_DIALOGS=1) and 0. */
int orta_vk_gate(void);
/* --setup-parent <hwnd>: the toolkit launcher's progress goes into that window. */
void orta_setup_set_parent(unsigned long long hwnd);

#ifdef __cplusplus
}
#endif

#endif
