/* audio.c - microphone capture (waveIn) into an in-memory PCM buffer. */
#include "app.h"
#include "core.h"

#include <stdlib.h>
#include <math.h>

#define VB_RATE      16000
#define VB_CHANNELS  1
#define VB_BITS      16
#define VB_NBUF      8
#define VB_BUFBYTES  3200      /* 100 ms of 16 kHz / 16 bit / mono */
#define VB_MAX_SECS  180

static HWND             s_notify;
static HWAVEIN          s_hwi;
static WAVEHDR          s_hdr[VB_NBUF];
static BYTE            *s_data[VB_NBUF];
static volatile LONG    s_level;
static volatile LONG    s_speech;
static volatile LONG64  s_last_voice;
static ULONGLONG        s_start;
static int              s_active;
static int              s_inited;
static int              s_gen;          /* bumps on every start, guards stale messages */

static BYTE            *s_pcm;
static DWORD            s_len;
static DWORD            s_cap;
static CRITICAL_SECTION s_cs;

static void ensure_inited(void)
{
    if (s_inited) return;
    InitializeCriticalSection(&s_cs);
    s_inited = 1;
}

int vb_audio_start(HWND notify)
{
    WAVEFORMATEX wf;
    MMRESULT mr;
    int i;

    ensure_inited();
    if (s_active) vb_audio_stop();

    s_notify = notify;
    EnterCriticalSection(&s_cs);
    s_len = 0;
    LeaveCriticalSection(&s_cs);
    InterlockedExchange(&s_level, 0);
    InterlockedExchange(&s_speech, 0);
    InterlockedExchange64(&s_last_voice, 0);
    s_start = vb_now_ms();
    s_gen++;

    ZeroMemory(&wf, sizeof(wf));
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = VB_CHANNELS;
    wf.nSamplesPerSec = VB_RATE;
    wf.wBitsPerSample = VB_BITS;
    wf.nBlockAlign = (WORD)(VB_CHANNELS * VB_BITS / 8);
    wf.nAvgBytesPerSec = VB_RATE * wf.nBlockAlign;
    wf.cbSize = 0;

    mr = waveInOpen(&s_hwi, WAVE_MAPPER, &wf, (DWORD_PTR)notify, 0, CALLBACK_WINDOW);
    if (mr != MMSYSERR_NOERROR) {
        s_hwi = NULL;
        vb_log(L"audio: waveInOpen failed (%u)", (unsigned)mr);
        return 0;
    }

    for (i = 0; i < VB_NBUF; i++) {
        s_data[i] = (BYTE *)malloc(VB_BUFBYTES);
        ZeroMemory(&s_hdr[i], sizeof(WAVEHDR));
        if (!s_data[i]) { vb_audio_stop(); return 0; }
        s_hdr[i].lpData = (LPSTR)s_data[i];
        s_hdr[i].dwBufferLength = VB_BUFBYTES;
        s_hdr[i].dwUser = (DWORD_PTR)s_gen;
        waveInPrepareHeader(s_hwi, &s_hdr[i], sizeof(WAVEHDR));
        waveInAddBuffer(s_hwi, &s_hdr[i], sizeof(WAVEHDR));
    }

    s_active = 1;
    mr = waveInStart(s_hwi);
    if (mr != MMSYSERR_NOERROR) {
        vb_log(L"audio: waveInStart failed (%u)", (unsigned)mr);
        vb_audio_stop();
        return 0;
    }
    return 1;
}

void vb_audio_on_data(WAVEHDR *hdr)
{
    DWORD i;
    const short *pcm;
    double acc = 0.0;
    DWORD n;

    if (!s_active || !hdr) return;
    if (hdr->dwUser != (DWORD_PTR)s_gen) return;   /* late message from a previous run */

    if (hdr->dwBytesRecorded > 0) {
        pcm = (const short *)hdr->lpData;
        n = hdr->dwBytesRecorded / 2;
        for (i = 0; i < n; i++) {
            double v = (double)pcm[i];
            acc += v * v;
        }
        if (n) {
            double rms = acc / (double)n;
            rms = (rms > 0.0) ? sqrt(rms) : 0.0;
            EnterCriticalSection(&s_cs);
            if (s_len + hdr->dwBytesRecorded <= (DWORD)(VB_MAX_SECS * VB_RATE * 2)) {
                if (s_len + hdr->dwBytesRecorded > s_cap) {
                    DWORD ncap = s_cap ? s_cap * 2 : 65536;
                    BYTE *np;
                    while (ncap < s_len + hdr->dwBytesRecorded) ncap *= 2;
                    np = (BYTE *)realloc(s_pcm, ncap);
                    if (np) { s_pcm = np; s_cap = ncap; }
                }
                if (s_len + hdr->dwBytesRecorded <= s_cap) {
                    memcpy(s_pcm + s_len, hdr->lpData, hdr->dwBytesRecorded);
                    s_len += hdr->dwBytesRecorded;
                }
            }
            LeaveCriticalSection(&s_cs);

            {
                int lvl = (int)(rms * 100.0 / 2500.0);
                if (lvl > 100) lvl = 100;
                if (rms < 150.0) lvl = 0;
                if (lvl > (int)InterlockedCompareExchange(&s_level, 0, 0))
                    InterlockedExchange(&s_level, (LONG)lvl);
            }
            if (rms > 400.0) {
                InterlockedExchange(&s_speech, 1);
                InterlockedExchange64(&s_last_voice, (LONG64)vb_now_ms());
            }
        }
    }

    if (s_active) {
        hdr->dwBytesRecorded = 0;
        waveInAddBuffer(s_hwi, hdr, sizeof(WAVEHDR));
    }
}

void vb_audio_stop(void)
{
    int i;
    if (!s_hwi) { s_active = 0; return; }
    s_active = 0;
    waveInStop(s_hwi);
    waveInReset(s_hwi);
    for (i = 0; i < VB_NBUF; i++) {
        waveInUnprepareHeader(s_hwi, &s_hdr[i], sizeof(WAVEHDR));
        if (s_data[i]) { free(s_data[i]); s_data[i] = NULL; }
        ZeroMemory(&s_hdr[i], sizeof(WAVEHDR));
    }
    waveInClose(s_hwi);
    s_hwi = NULL;
    InterlockedExchange(&s_level, 0);
}

BYTE *vb_audio_take_wav(DWORD *out_len)
{
    DWORD len;
    BYTE *wav;
    if (out_len) *out_len = 0;
    EnterCriticalSection(&s_cs);
    len = s_len;
    if (len == 0) { LeaveCriticalSection(&s_cs); return NULL; }
    wav = (BYTE *)malloc(44 + len);
    if (wav) {
        vb_wav_header(wav, len, VB_CHANNELS, VB_RATE, VB_BITS);
        memcpy(wav + 44, s_pcm, len);
        if (out_len) *out_len = 44 + len;
    }
    LeaveCriticalSection(&s_cs);
    return wav;
}

int vb_audio_level(void)            { return (int)InterlockedCompareExchange(&s_level, 0, 0); }
int vb_audio_speech_seen(void)      { return (int)InterlockedCompareExchange(&s_speech, 0, 0); }
ULONGLONG vb_audio_last_voice_ms(void) { return (ULONGLONG)InterlockedCompareExchange64(&s_last_voice, 0, 0); }
ULONGLONG vb_audio_start_ms(void)   { return s_start; }
int vb_audio_active(void)           { return s_active; }
