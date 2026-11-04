/* loopback_rec.c - dev tool (Windows only, never shipped): records what the default playback device plays
 * (WASAPI loopback) into a 16-bit or float WAV, for audio A/B comparisons (xemu reference vs the port).
 *
 *   loopback_rec.exe <out.wav> [seconds]      seconds = 0 or missing: until the file <out.wav>.stop exists
 *
 * Records the whole system mix (every program's sound), so close other audio sources first. The WAV keeps the
 * device's mix format (usually 48 kHz float stereo). Silence is written as silence (the loopback stream gives
 * no packets while nothing plays; the gap is filled from the elapsed time) so both recordings stay time-true.
 * Build: clang -O2 -o loopback_rec.exe loopback_rec.c -lole32 (llvm-mingw).
 * Origin: written for this port (2026-11-03) after the documented WASAPI loopback pattern
 * (Microsoft docs "Loopback Recording"); no third-party code. */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DEFINE_GUID(CLSID_MMDeviceEnumerator_, 0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
DEFINE_GUID(IID_IMMDeviceEnumerator_, 0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);
DEFINE_GUID(IID_IAudioClient_, 0x1CB9AD4C, 0xDBFA, 0x4C32, 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2);
DEFINE_GUID(IID_IAudioCaptureClient_, 0xC8ADBD64, 0xE71E, 0x48A0, 0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17);

static void put32(FILE *f, unsigned v) { fwrite(&v, 4, 1, f); }
static void put16(FILE *f, unsigned short v) { fwrite(&v, 2, 1, f); }

static void write_header(FILE *f, const WAVEFORMATEX *wf, unsigned data_bytes)
{
    unsigned short tag = wf->wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE) {
        const WAVEFORMATEXTENSIBLE *x = (const WAVEFORMATEXTENSIBLE *)wf;
        tag = (x->SubFormat.Data1 == 3) ? 3 : 1; /* KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : PCM */
    }
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f); put32(f, 36 + data_bytes); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, tag); put16(f, wf->nChannels); put32(f, wf->nSamplesPerSec);
    put32(f, wf->nAvgBytesPerSec); put16(f, wf->nBlockAlign); put16(f, wf->wBitsPerSample);
    fwrite("data", 1, 4, f); put32(f, data_bytes);
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: loopback_rec <out.wav> [seconds]\n"); return 2; }
    double secs = argc > 2 ? atof(argv[2]) : 0.0;
    char stop_path[MAX_PATH];
    snprintf(stop_path, sizeof stop_path, "%s.stop", argv[1]);
    DeleteFileA(stop_path);

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) { fprintf(stderr, "CoInitializeEx 0x%08lx\n", hr); return 1; }
    IMMDeviceEnumerator *en = NULL; IMMDevice *dev = NULL; IAudioClient *ac = NULL; IAudioCaptureClient *cc = NULL;
    WAVEFORMATEX *wf = NULL;
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator_, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator_, (void **)&en);
    if (SUCCEEDED(hr)) hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eRender, eConsole, &dev);
    if (SUCCEEDED(hr)) hr = IMMDevice_Activate(dev, &IID_IAudioClient_, CLSCTX_ALL, NULL, (void **)&ac);
    if (SUCCEEDED(hr)) hr = IAudioClient_GetMixFormat(ac, &wf);
    if (SUCCEEDED(hr)) hr = IAudioClient_Initialize(ac, AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                                                    10000000 /* 1 s buffer */, 0, wf, NULL);
    if (SUCCEEDED(hr)) hr = IAudioClient_GetService(ac, &IID_IAudioCaptureClient_, (void **)&cc);
    if (FAILED(hr)) { fprintf(stderr, "WASAPI loopback setup failed: 0x%08lx\n", hr); return 1; }

    FILE *f = fopen(argv[1], "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", argv[1]); return 1; }
    write_header(f, wf, 0);
    printf("recording %u Hz, %u ch, %u bit to %s (%s)\n", (unsigned)wf->nSamplesPerSec, wf->nChannels,
           wf->wBitsPerSample, argv[1], secs > 0 ? "timed" : "until .stop file");
    fflush(stdout);

    const unsigned block = wf->nBlockAlign;
    unsigned long long written_frames = 0;
    char *zero = calloc(1, (size_t)block * 4800);
    LARGE_INTEGER freq, t0, now;
    QueryPerformanceFrequency(&freq);
    IAudioClient_Start(ac);
    QueryPerformanceCounter(&t0);
    for (;;) {
        Sleep(10);
        QueryPerformanceCounter(&now);
        double el = (double)(now.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
        if (secs > 0 && el >= secs) break;
        if (secs <= 0 && GetFileAttributesA(stop_path) != INVALID_FILE_ATTRIBUTES) break;
        UINT32 n = 0;
        int got = 0;
        while (SUCCEEDED(IAudioCaptureClient_GetNextPacketSize(cc, &n)) && n > 0) {
            BYTE *data; UINT32 frames; DWORD flags;
            if (FAILED(IAudioCaptureClient_GetBuffer(cc, &data, &frames, &flags, NULL, NULL))) break;
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                for (UINT32 left = frames; left; ) { UINT32 k = left > 4800 ? 4800 : left; fwrite(zero, block, k, f); left -= k; }
            } else {
                fwrite(data, block, frames, f);
            }
            written_frames += frames;
            IAudioCaptureClient_ReleaseBuffer(cc, frames);
            got = 1;
        }
        if (!got && el > 0.1) { /* nothing plays: keep the file time-true with silence, 100 ms behind the clock
                                 * (el > 0.1 guard: a negative value cast to unsigned wrote 194 GB once) */
            unsigned long long due = (unsigned long long)((el - 0.1) * wf->nSamplesPerSec);
            while (written_frames < due) {
                unsigned long long k = due - written_frames; if (k > 4800) k = 4800;
                fwrite(zero, block, (size_t)k, f); written_frames += k;
            }
        }
    }
    IAudioClient_Stop(ac);
    write_header(f, wf, (unsigned)(written_frames * block));
    fclose(f);
    printf("done: %.1f s\n", (double)written_frames / wf->nSamplesPerSec);
    free(zero);
    CoTaskMemFree(wf);
    IAudioCaptureClient_Release(cc); IAudioClient_Release(ac); IMMDevice_Release(dev); IMMDeviceEnumerator_Release(en);
    CoUninitialize();
    return 0;
}
