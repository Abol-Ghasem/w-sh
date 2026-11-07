/* config.c - INI configuration handling. */
#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

VbConfig g_cfg;

static const WCHAR *k_default_ini =
    L"[general]\r\n"
    L"; Global shortcut. Modifiers: CTRL ALT SHIFT WIN. Key: A-Z, 0-9, F1-F24,\r\n"
    L"; SPACE, ENTER, TAB, ESC.\r\n"
    L"hotkey=CTRL+ALT+SPACE\r\n"
    L"\r\n"
    L"; Speech engine: auto | server | local | command\r\n"
    L";   auto    = server when api_key is set, then command, then local (Windows SAPI)\r\n"
    L";   server  = any OpenAI compatible /audio/transcriptions endpoint (works for Persian)\r\n"
    L";   local   = offline Windows Speech Recognition (English and a few other languages)\r\n"
    L";   command = run an external program (e.g. whisper.cpp) on the recorded wav file\r\n"
    L"engine=auto\r\n"
    L"\r\n"
    L"; Language hint for the server engine (fa, en, de, ...). Empty = auto detect.\r\n"
    L"language=\r\n"
    L"\r\n"
    L"; Paste the result into the window that was focused before the bar appeared.\r\n"
    L"auto_paste=1\r\n"
    L"\r\n"
    L"; 1 = paste immediately when transcription finishes, 0 = wait for Enter.\r\n"
    L"auto_accept=0\r\n"
    L"\r\n"
    L"; Finish automatically when the speaker stops talking.\r\n"
    L"stop_on_silence=1\r\n"
    L"silence_ms=1200\r\n"
    L"max_record_ms=120000\r\n"
    L"\r\n"
    L"[server]\r\n"
    L"base_url=https://api.openai.com/v1\r\n"
    L"api_key=\r\n"
    L"model=whisper-1\r\n"
    L"; Optional vocabulary / spelling hint sent to the server.\r\n"
    L"prompt=\r\n"
    L"\r\n"
    L"[command]\r\n"
    L"; %wav% is replaced with the recorded file. Stdout is used as the text.\r\n"
    L"; Example (whisper.cpp):\r\n"
    L";   line=C:\\tools\\whisper\\whisper-cli.exe -m C:\\tools\\whisper\\ggml-small.bin -l fa -nt -f \"%wav%\"\r\n"
    L"line=\r\n";

void vb_config_defaults(void)
{
    ZeroMemory(&g_cfg, sizeof(g_cfg));
    wcscpy(g_cfg.hotkey, L"CTRL+ALT+SPACE");
    g_cfg.hotkey_mods = MOD_CONTROL | MOD_ALT;
    g_cfg.hotkey_vk = VK_SPACE;
    wcscpy(g_cfg.language, L"");
    g_cfg.engine = VB_ENGINE_AUTO;
    g_cfg.auto_paste = 1;
    g_cfg.auto_accept = 0;
    g_cfg.stop_on_silence = 1;
    g_cfg.silence_ms = 1200;
    g_cfg.max_record_ms = 120000;
    wcscpy(g_cfg.base_url, L"https://api.openai.com/v1");
    g_cfg.api_key[0] = 0;
    wcscpy(g_cfg.model, L"whisper-1");
    g_cfg.prompt[0] = 0;
    g_cfg.command[0] = 0;
}

void vb_config_write_default(const WCHAR *path)
{
    HANDLE h;
    DWORD written = 0;
    const WCHAR bom = 0xFEFF;
    size_t body_len;

    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        vb_log(L"config: cannot create %s", path);
        return;
    }
    WriteFile(h, &bom, sizeof(WCHAR), &written, NULL);
    body_len = wcslen(k_default_ini) * sizeof(WCHAR);
    WriteFile(h, k_default_ini, (DWORD)body_len, &written, NULL);
    CloseHandle(h);
    vb_log(L"config: default ini written to %s", path);
}

static UINT key_from_name(const WCHAR *name)
{
    if (name[0] && !name[1]) {
        WCHAR c = name[0];
        if (c >= L'a' && c <= L'z') c = (WCHAR)(c - L'a' + L'A');
        if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) return (UINT)c;
    }
    if ((name[0] == L'F' || name[0] == L'f') && name[1]) {
        int n = _wtoi(name + 1);
        if (n >= 1 && n <= 24) return (UINT)(VK_F1 + n - 1);
    }
    if (!_wcsicmp(name, L"SPACE"))  return VK_SPACE;
    if (!_wcsicmp(name, L"ENTER"))  return VK_RETURN;
    if (!_wcsicmp(name, L"TAB"))    return VK_TAB;
    if (!_wcsicmp(name, L"ESC"))    return VK_ESCAPE;
    if (!_wcsicmp(name, L"INSERT")) return VK_INSERT;
    if (!_wcsicmp(name, L"HOME"))   return VK_HOME;
    if (!_wcsicmp(name, L"END"))    return VK_END;
    if (!_wcsicmp(name, L"PAUSE"))  return VK_PAUSE;
    if (!_wcsicmp(name, L"SCROLL")) return VK_SCROLL;
    if (!_wcsicmp(name, L"`"))      return VK_OEM_3;
    if (!_wcsicmp(name, L"-"))      return VK_OEM_MINUS;
    if (!_wcsicmp(name, L"="))      return VK_OEM_PLUS;
    if (!_wcsicmp(name, L","))      return VK_OEM_COMMA;
    if (!_wcsicmp(name, L"."))      return VK_OEM_PERIOD;
    return 0;
}

int vb_hotkey_parse(const WCHAR *spec, UINT *mods, UINT *vk)
{
    WCHAR buf[128];
    WCHAR *ctx = NULL;
    WCHAR *tok;
    UINT m = 0, k = 0;
    int count = 0;

    if (!spec || !*spec) return 0;
    wcsncpy(buf, spec, 127);
    buf[127] = 0;

    for (tok = wcstok(buf, L" +\t", &ctx); tok; tok = wcstok(NULL, L" +\t", &ctx)) {
        count++;
        if (!_wcsicmp(tok, L"CTRL") || !_wcsicmp(tok, L"CONTROL")) m |= MOD_CONTROL;
        else if (!_wcsicmp(tok, L"ALT"))  m |= MOD_ALT;
        else if (!_wcsicmp(tok, L"SHIFT")) m |= MOD_SHIFT;
        else if (!_wcsicmp(tok, L"WIN") || !_wcsicmp(tok, L"WINDOWS")) m |= MOD_WIN;
        else {
            k = key_from_name(tok);
            if (!k) { vb_log(L"config: unknown hotkey part '%s'", tok); return 0; }
        }
    }
    if (count < 2 || !k) return 0;
    *mods = m;
    *vk = k;
    return 1;
}

void vb_config_reload(void);

void vb_config_reload(void)
{
    WCHAR exe[MAX_PATH], path[MAX_PATH];
    WCHAR tmp[64];

    vb_config_defaults();
    vb_exe_dir(exe, MAX_PATH);
    _snwprintf(g_cfg.ini_path, MAX_PATH, L"%s\\VoiceBar.ini", exe);
    wcscpy(path, g_cfg.ini_path);

    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        vb_config_write_default(path);

    vb_ini_get(path, L"general", L"hotkey", L"CTRL+ALT+SPACE", g_cfg.hotkey, 64);
    vb_ini_get(path, L"general", L"language", L"", g_cfg.language, 16);
    vb_ini_get(path, L"general", L"engine", L"auto", tmp, 64);
    if (!_wcsicmp(tmp, L"local"))        g_cfg.engine = VB_ENGINE_LOCAL;
    else if (!_wcsicmp(tmp, L"server"))  g_cfg.engine = VB_ENGINE_SERVER;
    else if (!_wcsicmp(tmp, L"command")) g_cfg.engine = VB_ENGINE_COMMAND;
    else                                 g_cfg.engine = VB_ENGINE_AUTO;

    g_cfg.auto_paste       = vb_ini_get_int(path, L"general", L"auto_paste", 1);
    g_cfg.auto_accept      = vb_ini_get_int(path, L"general", L"auto_accept", 0);
    g_cfg.stop_on_silence  = vb_ini_get_int(path, L"general", L"stop_on_silence", 1);
    g_cfg.silence_ms       = vb_ini_get_int(path, L"general", L"silence_ms", 1200);
    g_cfg.max_record_ms    = vb_ini_get_int(path, L"general", L"max_record_ms", 120000);
    if (g_cfg.silence_ms < 300) g_cfg.silence_ms = 300;
    if (g_cfg.max_record_ms < 2000) g_cfg.max_record_ms = 2000;

    vb_ini_get(path, L"server", L"base_url", L"https://api.openai.com/v1", g_cfg.base_url, 512);
    vb_ini_get(path, L"server", L"api_key", L"", g_cfg.api_key, 512);
    vb_ini_get(path, L"server", L"model", L"whisper-1", g_cfg.model, 128);
    vb_ini_get(path, L"server", L"prompt", L"", g_cfg.prompt, 256);
    vb_ini_get(path, L"command", L"line", L"", g_cfg.command, 1024);

    if (!vb_hotkey_parse(g_cfg.hotkey, &g_cfg.hotkey_mods, &g_cfg.hotkey_vk)) {
        g_cfg.hotkey_mods = MOD_CONTROL | MOD_ALT;
        g_cfg.hotkey_vk = VK_SPACE;
        vb_log(L"config: bad hotkey '%s', using CTRL+ALT+SPACE", g_cfg.hotkey);
    }
}

int vb_config_load(void)
{
    vb_config_reload();
    return 1;
}
