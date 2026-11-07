/* util.c - small Win32 helpers: logging, clipboard, paths, encoding. */
#include "app.h"
#include "core.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

static WCHAR g_log_path[MAX_PATH];
static int   g_log_ready;

void vb_exe_dir(WCHAR *out, int cch)
{
    DWORD n;
    if (!out || cch <= 0) return;
    n = GetModuleFileNameW(NULL, out, (DWORD)cch);
    if (n == 0) { out[0] = 0; return; }
    while (n > 0 && out[n - 1] != L'\\' && out[n - 1] != L'/') n--;
    if (n > 0) n--;
    out[n] = 0;
}

WCHAR *vb_wcsdup(const WCHAR *s)
{
    size_t n;
    WCHAR *p;
    if (!s) return NULL;
    n = wcslen(s) + 1;
    p = (WCHAR *)malloc(n * sizeof(WCHAR));
    if (p) memcpy(p, s, n * sizeof(WCHAR));
    return p;
}

WCHAR *vb_utf8_to_wide_alloc(const char *s)
{
    uint16_t *u;
    size_t n = 0;
    if (!s) return NULL;
    u = vb_utf8_to_utf16(s, &n);
    if (!u) return NULL;
    return (WCHAR *)u;   /* identical layout on Windows */
}

ULONGLONG vb_now_ms(void)
{
    return GetTickCount64();
}

void vb_log(const WCHAR *fmt, ...)
{
    WCHAR line[2048];
    WCHAR stamp[64];
    SYSTEMTIME st;
    int n;
    va_list ap;
    HANDLE h;
    DWORD written;

    if (!g_log_ready) {
        WCHAR dir[MAX_PATH];
        g_log_ready = 1;
        vb_exe_dir(dir, MAX_PATH);
        _snwprintf(g_log_path, MAX_PATH, L"%s\\VoiceBar.log", dir);
    }

    va_start(ap, fmt);
    n = _vsnwprintf(line, 2000, fmt, ap);
    va_end(ap);
    if (n < 0) n = 2000;
    line[n] = 0;

    GetLocalTime(&st);
    _snwprintf(stamp, 64, L"[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute,
               st.wSecond, st.wMilliseconds);

    h = CreateFileW(g_log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, NULL, FILE_END);
    WriteFile(h, stamp, (DWORD)(wcslen(stamp) * sizeof(WCHAR)), &written, NULL);
    WriteFile(h, line, (DWORD)(wcslen(line) * sizeof(WCHAR)), &written, NULL);
    WriteFile(h, L"\r\n", 2 * sizeof(WCHAR), &written, NULL);
    CloseHandle(h);
}

BOOL vb_set_clipboard_text(HWND owner, const WCHAR *text)
{
    int tries;
    size_t bytes;
    HGLOBAL mem;
    void *dst;
    if (!text) return FALSE;
    bytes = (wcslen(text) + 1) * sizeof(WCHAR);
    for (tries = 0; tries < 10; tries++) {
        if (OpenClipboard(owner)) break;
        Sleep(20);
    }
    if (tries == 10) return FALSE;
    EmptyClipboard();
    mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) { CloseClipboard(); return FALSE; }
    dst = GlobalLock(mem);
    if (!dst) { GlobalFree(mem); CloseClipboard(); return FALSE; }
    memcpy(dst, text, bytes);
    GlobalUnlock(mem);
    if (!SetClipboardData(CF_UNICODETEXT, mem)) {
        GlobalFree(mem);
        CloseClipboard();
        return FALSE;
    }
    CloseClipboard();
    return TRUE;
}

/* Sends Ctrl+V to whatever window currently has the keyboard focus. */
void vb_send_paste(void)
{
    INPUT in[4];
    ZeroMemory(in, sizeof(in));

    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = VK_CONTROL;
    in[1].type = INPUT_KEYBOARD;
    in[1].ki.wVk = 'V';
    in[2].type = INPUT_KEYBOARD;
    in[2].ki.wVk = 'V';
    in[2].ki.dwFlags = KEYEVENTF_KEYUP;
    in[3].type = INPUT_KEYBOARD;
    in[3].ki.wVk = VK_CONTROL;
    in[3].ki.dwFlags = KEYEVENTF_KEYUP;

    SendInput(4, in, sizeof(INPUT));
}

void vb_ini_get(const WCHAR *file, const WCHAR *section, const WCHAR *key,
                const WCHAR *def, WCHAR *out, DWORD cch)
{
    if (out == NULL || cch == 0) return;
    out[0] = 0;
    GetPrivateProfileStringW(section, key, def ? def : L"", out, cch, file);
}

int vb_ini_get_int(const WCHAR *file, const WCHAR *section, const WCHAR *key, int def)
{
    return (int)GetPrivateProfileIntW(section, key, def, file);
}
