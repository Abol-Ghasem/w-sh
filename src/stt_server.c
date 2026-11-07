/* stt_server.c - upload the recorded wav to an OpenAI compatible
 * /audio/transcriptions endpoint (OpenAI, Groq, LocalAI, faster-whisper-server,
 * whisper.cpp server ...). Works in a background thread. */
#include "app.h"
#include "core.h"

#include <winhttp.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>

typedef struct {
    BYTE *wav;
    DWORD len;
} ServerJob;

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
    vb_log(L"server: %s", buf);
    post_result(1, vb_wcsdup(buf));
}

/* Reads the whole body of a WinHTTP response into a malloc'ed string. */
static char *read_response(HINTERNET req, DWORD *out_len)
{
    char *buf = NULL;
    DWORD cap = 0, len = 0;
    for (;;) {
        DWORD avail = 0, got = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) break;
        if (avail == 0) break;
        if (len + avail + 1 > cap) {
            DWORD ncap = cap ? cap : 8192;
            char *nb;
            while (ncap < len + avail + 1) ncap *= 2;
            nb = (char *)realloc(buf, ncap);
            if (!nb) break;
            buf = nb;
            cap = ncap;
        }
        if (!WinHttpReadData(req, buf + len, avail, &got)) break;
        len += got;
        if (got == 0) break;
        if (len > 4 * 1024 * 1024) break;
    }
    if (!buf) buf = (char *)malloc(1);
    if (buf) buf[len] = 0;
    if (out_len) *out_len = len;
    return buf;
}

static DWORD WINAPI server_worker(LPVOID param)
{
    ServerJob *job = (ServerJob *)param;
    WCHAR url[1024];
    WCHAR host[256], path[1024];
    WCHAR headers[1024];
    WCHAR boundary[64];
    URL_COMPONENTS uc;
    HINTERNET sess = NULL, conn = NULL, req = NULL;
    uint8_t *body = NULL;
    uint32_t body_len = 0;
    char *resp = NULL;
    const char *names[3], *values[3];
    VbMultipart mp;
    int nfields = 0;
    char *text_utf8 = NULL;
    DWORD status = 0;
    size_t base_len;
    BOOL ok = FALSE;

    /* --- build the endpoint URL --------------------------------------- */
    wcsncpy(url, g_cfg.base_url, 1000);
    url[1000] = 0;
    base_len = wcslen(url);
    while (base_len > 0 && url[base_len - 1] == L'/') url[--base_len] = 0;
    if (!wcsstr(url, L"/audio/")) {
        if (base_len + 22 < 1000) wcscat(url, L"/audio/transcriptions");
    }

    /* --- build the multipart body -------------------------------------- */
    _snwprintf(boundary, 64, L"----VoiceBar%08X", (unsigned)GetTickCount());
    {
        char *bnd_u8 = (char *)vb_utf16_to_utf8((const uint16_t *)boundary, wcslen(boundary));
        char *model_u8 = (char *)vb_utf16_to_utf8((const uint16_t *)g_cfg.model, wcslen(g_cfg.model));
        char *lang_u8 = (char *)vb_utf16_to_utf8((const uint16_t *)g_cfg.language, wcslen(g_cfg.language));
        char *prompt_u8 = (char *)vb_utf16_to_utf8((const uint16_t *)g_cfg.prompt, wcslen(g_cfg.prompt));

        names[0] = "model";    values[0] = model_u8 ? model_u8 : "whisper-1"; nfields = 1;
        if (lang_u8 && *lang_u8)   { names[nfields] = "language"; values[nfields] = lang_u8;   nfields++; }
        if (prompt_u8 && *prompt_u8) { names[nfields] = "prompt"; values[nfields] = prompt_u8; nfields++; }

        ZeroMemory(&mp, sizeof(mp));
        mp.file_field = "file";
        mp.file_name = "audio.wav";
        mp.file_type = "audio/wav";
        mp.file_data = job->wav;
        mp.file_len = job->len;
        mp.names = names;
        mp.values = values;
        mp.n = nfields;

        if (bnd_u8) {
            body = vb_multipart_body(bnd_u8, &mp, &body_len);
            free(bnd_u8);
        }
        free(model_u8);
        free(lang_u8);
        free(prompt_u8);
    }
    if (!body) { post_error(L"Out of memory while building the request."); goto done; }

    /* --- open HTTP session --------------------------------------------- */
    ZeroMemory(&uc, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.dwHostNameLength = (DWORD)-1;
    uc.dwUrlPathLength = (DWORD)-1;
    uc.dwExtraInfoLength = (DWORD)-1;
    uc.dwSchemeLength = (DWORD)-1;
    if (!WinHttpCrackUrl(url, 0, 0, &uc)) {
        post_error(L"Invalid base_url in VoiceBar.ini: %s", url);
        goto done;
    }
    {
        DWORD hl = uc.dwHostNameLength < 255 ? uc.dwHostNameLength : 255;
        DWORD pl = uc.dwUrlPathLength < 1000 ? uc.dwUrlPathLength : 1000;
        memcpy(host, uc.lpszHostName, hl * sizeof(WCHAR));
        host[hl] = 0;
        memcpy(path, uc.lpszUrlPath, pl * sizeof(WCHAR));
        path[pl] = 0;
        if (uc.dwExtraInfoLength && pl + uc.dwExtraInfoLength + 1 < 1000) {
            memcpy(path + pl, uc.lpszExtraInfo, uc.dwExtraInfoLength * sizeof(WCHAR));
            path[pl + uc.dwExtraInfoLength] = 0;
        }
    }

    sess = WinHttpOpen(L"VoiceBar/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!sess) { post_error(L"WinHttpOpen failed (%lu)", GetLastError()); goto done; }
    WinHttpSetTimeouts(sess, 15000, 15000, 30000, 120000);

    conn = WinHttpConnect(sess, host, (INTERNET_PORT)(uc.nPort ? uc.nPort : 443), 0);
    if (!conn) { post_error(L"Cannot connect to %s (%lu)", host, GetLastError()); goto done; }

    req = WinHttpOpenRequest(conn, L"POST", path, NULL, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                             uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!req) { post_error(L"WinHttpOpenRequest failed (%lu)", GetLastError()); goto done; }

    _snwprintf(headers, 1000,
               L"Content-Type: multipart/form-data; boundary=%s\r\n", boundary);
    WinHttpAddRequestHeaders(req, headers, (DWORD)-1L,
                             WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    if (g_cfg.api_key[0]) {
        _snwprintf(headers, 1000, L"Authorization: Bearer %s\r\n", g_cfg.api_key);
        WinHttpAddRequestHeaders(req, headers, (DWORD)-1L,
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, body_len, 0);
    if (ok && body_len) {
        DWORD written = 0;
        ok = WinHttpWriteData(req, body, body_len, &written);
    }
    if (!ok) {
        post_error(L"Cannot reach %s (%lu)", host, GetLastError());
        goto done;
    }
    if (!WinHttpReceiveResponse(req, NULL)) {
        post_error(L"No response from %s (%lu)", host, GetLastError());
        goto done;
    }

    {
        DWORD sz = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    }

    resp = read_response(req, NULL);

    if (status >= 200 && status < 300) {
        text_utf8 = vb_json_get_string(resp ? resp : "", "text");
        if (!text_utf8) {
            post_error(L"Unexpected server answer: %.300s", resp ? resp : "");
            goto done;
        }
        {
            WCHAR *w = vb_utf8_to_wide_alloc(text_utf8);
            if (!w) { post_error(L"Out of memory."); goto done; }
            post_result(0, w);
        }
    } else {
        const char *keys[2] = { "message", "error" };
        char *msg = vb_json_get_first_string(resp ? resp : "", keys, 2);
        if (msg) {
            WCHAR *w = vb_utf8_to_wide_alloc(msg);
            post_error(L"HTTP %lu: %s", status, w ? w : L"?");
            free(w);
            free(msg);
        } else {
            post_error(L"HTTP %lu from %s", status, host);
        }
    }

done:
    if (req) WinHttpCloseHandle(req);
    if (conn) WinHttpCloseHandle(conn);
    if (sess) WinHttpCloseHandle(sess);
    free(body);
    free(resp);
    free(text_utf8);
    if (job->wav) free(job->wav);
    free(job);
    return 0;
}

void vb_server_transcribe(BYTE *wav, DWORD len)
{
    ServerJob *job;
    HANDLE th;
    if (!wav || !len) return;
    job = (ServerJob *)malloc(sizeof(ServerJob));
    if (!job) { free(wav); return; }
    job->wav = wav;
    job->len = len;
    th = CreateThread(NULL, 0, server_worker, job, 0, NULL);
    if (!th) {
        free(wav);
        free(job);
        return;
    }
    CloseHandle(th);
}
