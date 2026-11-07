/* stt_cmd.c - transcribe by running an external command (whisper.cpp,
 * faster-whisper, whisper-ctranslate2, vosk ...). %wav% in the command line is
 * replaced with the recorded file; stdout becomes the text. */
#include "app.h"
#include "core.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static void post_result(int is_error, WCHAR *text)
{
    if (!PostMessageW(g_hwnd_main, WM_VB_RESULT, (WPARAM)(is_error ? 1 : 0), (LPARAM)text))
        free(text);
}

static void post_error(const WCHAR *fmt, ...)
{
    WCHAR buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, 1000, fmt, ap);
    va_end(ap);
    buf[1000] = 0;
    vb_log(L"command: %s", buf);
    post_result(1, vb_wcsdup(buf));
}

static WCHAR *trim(WCHAR *s)
{
    size_t n;
    while (*s == L' ' || *s == L'\r' || *s == L'\n' || *s == L'\t') s++;
    n = wcslen(s);
    while (n > 0 && (s[n - 1] == L' ' || s[n - 1] == L'\r' ||
                     s[n - 1] == L'\n' || s[n - 1] == L'\t')) s[--n] = 0;
    return s;
}

static WCHAR *expand_command(const WCHAR *tmpl, const WCHAR *wav_path)
{
    size_t cap = wcslen(tmpl) + wcslen(wav_path) * 4 + 64;
    WCHAR *out = (WCHAR *)malloc(cap * sizeof(WCHAR));
    const WCHAR *p = tmpl;
    WCHAR *q;
    if (!out) return NULL;
    q = out;
    while (*p) {
        if (p[0] == L'%' && _wcsnicmp(p, L"%wav%", 5) == 0) {
            wcscpy(q, wav_path);
            q += wcslen(wav_path);
            p += 5;
        } else {
            *q++ = *p++;
        }
    }
    *q = 0;
    return out;
}

typedef struct {
    BYTE *wav;
    DWORD len;
} CmdJob;

static DWORD WINAPI cmd_worker(LPVOID param)
{
    CmdJob *job = (CmdJob *)param;
    WCHAR temp_dir[MAX_PATH], wav_path[MAX_PATH], cmdline[2048];
    HANDLE f, rd = NULL, wr = NULL;
    SECURITY_ATTRIBUTES sa;
    DWORD written = 0, total = 0;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    char *out = NULL;
    size_t i;

    GetTempPathW(MAX_PATH - 40, temp_dir);
    _snwprintf(wav_path, MAX_PATH, L"%svoicebar_%lu.wav", temp_dir,
               (unsigned long)GetCurrentProcessId());

    f = CreateFileW(wav_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        post_error(L"Cannot write %s", wav_path);
        goto done;
    }
    WriteFile(f, job->wav, job->len, &written, NULL);
    CloseHandle(f);

    {
        WCHAR *expanded = expand_command(g_cfg.command, wav_path);
        if (!expanded) { DeleteFileW(wav_path); post_error(L"Out of memory."); goto done; }
        wcsncpy(cmdline, expanded, 2000);
        cmdline[2000] = 0;
        free(expanded);
    }

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&rd, &wr, &sa, 0)) {
        DeleteFileW(wav_path);
        post_error(L"CreatePipe failed (%lu)", GetLastError());
        goto done;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = CreateFileW(L"NUL", GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                               OPEN_EXISTING, 0, NULL);
    if (si.hStdInput == INVALID_HANDLE_VALUE) si.hStdInput = NULL;
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessW(NULL, cmdline, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi)) {
        CloseHandle(rd);
        CloseHandle(wr);
        DeleteFileW(wav_path);
        post_error(L"Cannot start the command (%lu)", GetLastError());
        goto done;
    }
    CloseHandle(wr);
    if (si.hStdInput) CloseHandle(si.hStdInput);

    out = (char *)malloc(65536);
    if (out) {
        DWORD got = 0;
        while (total < 65000 && ReadFile(rd, out + total, 60000 - total, &got, NULL) && got > 0)
            total += got;
        out[total] = 0;
    }
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, 300000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    DeleteFileW(wav_path);

    if (!out) { post_error(L"Out of memory."); goto done; }

    for (i = 0; i < total; i++)
        if (out[i] == '\r' || out[i] == '\n' || out[i] == '\t') out[i] = ' ';

    {
        WCHAR *w = vb_utf8_to_wide_alloc(out);
        if (!w) { post_error(L"Out of memory."); goto done; }
        post_result(0, vb_wcsdup(trim(w)));
        free(w);
    }
done:
    free(out);
    if (job->wav) free(job->wav);
    free(job);
    return 0;
}

void vb_command_transcribe(BYTE *wav, DWORD len)
{
    CmdJob *job;
    HANDLE th;
    if (!wav || !len) return;
    if (!g_cfg.command[0]) {
        free(wav);
        post_error(L"No [command] line configured in VoiceBar.ini");
        return;
    }
    job = (CmdJob *)malloc(sizeof(CmdJob));
    if (!job) { free(wav); return; }
    job->wav = wav;
    job->len = len;
    th = CreateThread(NULL, 0, cmd_worker, job, 0, NULL);
    if (!th) { free(wav); free(job); return; }
    CloseHandle(th);
}
