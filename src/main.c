/* main.c - VoiceBar entry point, hotkey, tray icon and the state machine. */
#include "app.h"
#include "core.h"

#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>

HINSTANCE g_hinst;
HWND      g_hwnd_main;
int       g_state = VB_ST_IDLE;

static HWND             s_prev_focus;
static HICON            s_icon;
static NOTIFYICONDATAW  s_nid;
static UINT             s_msg_taskbar_created;
static int              s_hotkey_ok;
static int              s_engine_used = VB_ENGINE_LOCAL;
static int              s_quitting;
static int              s_discard_result;
static int              s_first_run;

#define VB_HOTKEY_ID 1

/* ------------------------------------------------------------- utilities */

static int effective_engine(void)
{
    if (g_cfg.engine == VB_ENGINE_AUTO) {
        if (g_cfg.api_key[0]) return VB_ENGINE_SERVER;
        if (g_cfg.command[0]) return VB_ENGINE_COMMAND;
        return VB_ENGINE_LOCAL;
    }
    return g_cfg.engine;
}

static void tray_add(void)
{
    ZeroMemory(&s_nid, sizeof(s_nid));
    s_nid.cbSize = sizeof(s_nid);
    s_nid.hWnd = g_hwnd_main;
    s_nid.uID = VB_TRAY_ID;
    s_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    s_nid.uCallbackMessage = WM_VB_TRAY;
    s_nid.hIcon = s_icon ? s_icon : LoadIconW(NULL, IDI_APPLICATION);
    _snwprintf(s_nid.szTip, 127, L"%s %s  (%s)", VB_APP_NAME, VB_VERSION, g_cfg.hotkey);
    s_nid.szTip[127] = 0;
    Shell_NotifyIconW(NIM_DELETE, &s_nid);
    Shell_NotifyIconW(NIM_ADD, &s_nid);
    s_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &s_nid);
}

static void tray_remove(void)
{
    Shell_NotifyIconW(NIM_DELETE, &s_nid);
}

#define VB_RUN_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define VB_RUN_VAL L"VoiceBar"

static int autostart_enabled(void)
{
    HKEY key;
    WCHAR buf[MAX_PATH * 2];
    DWORD sz = sizeof(buf);
    int on = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, VB_RUN_KEY, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExW(key, VB_RUN_VAL, NULL, NULL, (BYTE *)buf, &sz) == ERROR_SUCCESS)
            on = 1;
        RegCloseKey(key);
    }
    return on;
}

static void autostart_set(int on)
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, VB_RUN_KEY, 0, NULL, 0, KEY_SET_VALUE,
                        NULL, &key, NULL) != ERROR_SUCCESS)
        return;
    if (on) {
        WCHAR exe[MAX_PATH], val[MAX_PATH + 8];
        vb_exe_dir(exe, MAX_PATH);
        _snwprintf(val, MAX_PATH + 6, L"\"%s\\VoiceBar.exe\"", exe);
        val[MAX_PATH + 6] = 0;
        RegSetValueExW(key, VB_RUN_VAL, 0, REG_SZ, (const BYTE *)val,
                       (DWORD)((wcslen(val) + 1) * sizeof(WCHAR)));
    } else {
        RegDeleteValueW(key, VB_RUN_VAL);
    }
    RegCloseKey(key);
}

static void open_in_notepad(const WCHAR *path)
{
    ShellExecuteW(NULL, L"open", L"notepad.exe", path, NULL, SW_SHOWNORMAL);
}

static void show_tray_menu(void)
{
    HMENU menu = CreatePopupMenu();
    POINT pt;
    WCHAR log_path[MAX_PATH], exe[MAX_PATH];
    int cmd;
    log_path[0] = 0;

    AppendMenuW(menu, MF_STRING, VB_CMD_SHOW, L"نمایش نوار");
    AppendMenuW(menu, MF_STRING | (autostart_enabled() ? MF_CHECKED : MF_UNCHECKED),
                VB_CMD_AUTOSTART, L"اجرای خودکار با ویندوز");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, VB_CMD_SETTINGS, L"تنظیمات (VoiceBar.ini)");
    AppendMenuW(menu, MF_STRING, VB_CMD_RELOAD, L"بارگذاری مجدد تنظیمات");
    {
        vb_exe_dir(exe, MAX_PATH);
        _snwprintf(log_path, MAX_PATH, L"%s\\VoiceBar.log", exe);
        AppendMenuW(menu, MF_STRING, VB_CMD_EXIT + 200, L"مشاهده فایل گزارش");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, VB_CMD_EXIT, L"خروج");

    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd_main);
    cmd = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                              pt.x, pt.y, 0, g_hwnd_main, NULL);
    DestroyMenu(menu);

    switch (cmd) {
    case VB_CMD_SHOW:     vb_app_on_hotkey(); break;
    case VB_CMD_SETTINGS:
        if (GetFileAttributesW(g_cfg.ini_path) == INVALID_FILE_ATTRIBUTES)
            vb_config_write_default(g_cfg.ini_path);
        open_in_notepad(g_cfg.ini_path);
        break;
    case VB_CMD_RELOAD:   vb_app_reload_config(); break;
    case VB_CMD_AUTOSTART: autostart_set(!autostart_enabled()); break;
    case VB_CMD_EXIT + 200: open_in_notepad(log_path); break;
    case VB_CMD_EXIT:     vb_app_quit(); break;
    default: break;
    }
}

/* ---------------------------------------------------------- state machine */

void vb_app_start_listen(void)
{
    int eng = effective_engine();
    s_engine_used = eng;

    vb_ui_reset();

    if (eng == VB_ENGINE_LOCAL) {
        if (!vb_local_start()) {
            vb_ui_set_status_error(L"موتور آفلاین ویندوز پیدا نشد — در VoiceBar.ini کلید api_key یا بخش [command] را تنظیم کنید");
            g_state = VB_ST_LISTENING;   /* Esc / hotkey still closes the bar */
            return;
        }
        vb_ui_set_status(L"در حال شنیدن (تشخیص گفتار ویندوز) — Enter پایان، Esc انصراف");
        g_state = VB_ST_LISTENING;
        return;
    }

    if (!vb_audio_start(g_hwnd_main)) {
        vb_ui_set_status_error(L"میکروفون در دسترس نیست");
        g_state = VB_ST_IDLE;
        return;
    }
    if (eng == VB_ENGINE_COMMAND)
        vb_ui_set_status(L"در حال شنیدن — Enter پایان، Esc انصراف");
    else
        vb_ui_set_status(L"در حال شنیدن — صحبت کنید، با سکوت خودکار تمام می‌شود (Enter پایان، Esc انصراف)");
    g_state = VB_ST_LISTENING;
}

void vb_app_stop_and_transcribe(void)
{
    if (g_state != VB_ST_LISTENING) return;

    if (s_engine_used == VB_ENGINE_LOCAL) {
        vb_local_stop();
        vb_ui_set_level(0);
        g_state = VB_ST_IDLE;
        if (g_cfg.auto_accept) {
            vb_app_accept();
        } else {
            vb_ui_set_status(L"Enter برای چسباندن، Esc برای انصراف");
        }
        return;
    }

    {
        BYTE *wav;
        DWORD len = 0;
        vb_audio_stop();
        vb_ui_set_level(0);
        wav = vb_audio_take_wav(&len);
        if (!wav || len < 44 + 4000) {
            if (wav) free(wav);
            g_state = VB_ST_IDLE;
            vb_ui_set_status_error(L"صدایی ضبط نشد — دوباره کلید میان‌بر را بزنید و صحبت کنید");
            return;
        }
        g_state = VB_ST_BUSY;
        vb_ui_set_status(s_engine_used == VB_ENGINE_COMMAND
                             ? L"در حال تبدیل با برنامه محلی…"
                             : L"در حال تبدیل به متن…");
        if (s_engine_used == VB_ENGINE_COMMAND) vb_command_transcribe(wav, len);
        else                                    vb_server_transcribe(wav, len);
    }
}

void vb_app_accept(void)
{
    static WCHAR text[65536];
    HWND target;

    vb_ui_get_text(text, 65536);
    if (!text[0]) {
        vb_ui_hide();
        g_state = VB_ST_IDLE;
        return;
    }
    vb_set_clipboard_text(g_hwnd_main, text);

    target = s_prev_focus;
    vb_ui_hide();
    g_state = VB_ST_IDLE;
    s_prev_focus = NULL;

    if (g_cfg.auto_paste && target && IsWindow(target)) {
        SetForegroundWindow(target);
        Sleep(90);
        vb_send_paste();
    }
    vb_log(L"accepted %u chars", (unsigned)wcslen(text));
}

void vb_app_cancel(void)
{
    if (g_state == VB_ST_BUSY) s_discard_result = 1;
    if (s_engine_used == VB_ENGINE_LOCAL) vb_local_stop();
    else vb_audio_stop();
    vb_ui_set_level(0);
    vb_ui_hide();
    g_state = VB_ST_IDLE;
    s_prev_focus = NULL;
}

void vb_app_on_hotkey(void)
{
    if (g_state == VB_ST_LISTENING) {
        vb_app_stop_and_transcribe();
        return;
    }
    if (g_state == VB_ST_BUSY) {
        return;   /* wait for the engine */
    }

    /* remember where the text should go; keep the old target if our own
       window already has the focus (e.g. the tray menu was used) */
    {
        HWND fg = GetForegroundWindow();
        WCHAR cls[64];
        cls[0] = 0;
        if (fg) GetClassNameW(fg, cls, 64);
        if (fg && fg != g_hwnd_main && _wcsicmp(cls, VB_CLASS_BAR) != 0)
            s_prev_focus = fg;
        else if (s_prev_focus && !IsWindow(s_prev_focus))
            s_prev_focus = NULL;
    }
    s_discard_result = 0;

    vb_ui_show();
    vb_app_start_listen();
}

void vb_app_on_enter(void)
{
    if (g_state == VB_ST_LISTENING) {
        vb_app_stop_and_transcribe();
        return;
    }
    if (g_state == VB_ST_BUSY) return;
    vb_app_accept();
}

void vb_app_on_escape(void)
{
    vb_app_cancel();
}

void vb_app_reload_config(void)
{
    if (s_hotkey_ok) {
        UnregisterHotKey(g_hwnd_main, VB_HOTKEY_ID);
        s_hotkey_ok = 0;
    }
    vb_config_reload();
    s_hotkey_ok = RegisterHotKey(g_hwnd_main, VB_HOTKEY_ID,
                                 g_cfg.hotkey_mods | MOD_NOREPEAT, g_cfg.hotkey_vk) ? 1 : 0;
    if (!s_hotkey_ok)
        vb_log(L"hotkey %s could not be registered", g_cfg.hotkey);
    tray_add();
    vb_log(L"config reloaded (hotkey=%s engine=%d)", g_cfg.hotkey, effective_engine());
}

void vb_app_quit(void)
{
    s_quitting = 1;
    vb_audio_stop();
    vb_local_shutdown();
    tray_remove();
    if (s_hotkey_ok) UnregisterHotKey(g_hwnd_main, VB_HOTKEY_ID);
    DestroyWindow(g_hwnd_main);
}

/* ---------------------------------------------------------------- window */

static void on_result(int is_error, WCHAR *text)
{
    if (g_state == VB_ST_BUSY) g_state = VB_ST_IDLE;
    if (!text) return;
    if (s_discard_result) {
        s_discard_result = 0;
        free(text);
        return;
    }

    if (is_error) {
        vb_ui_set_status_error(text);
    } else {
        if (text[0]) {
            vb_ui_set_text(text);
            vb_ui_set_status(L"Enter برای چسباندن، Esc برای انصراف");
        } else {
            vb_ui_set_status_error(L"چیزی تشخیص داده نشد — دوباره تلاش کنید");
        }
        if (text[0] && g_cfg.auto_accept) {
            free(text);
            vb_app_accept();
            return;
        }
    }
    free(text);
}

static LRESULT CALLBACK main_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (s_msg_taskbar_created && msg == s_msg_taskbar_created) {
        tray_add();
        return 0;
    }

    switch (msg) {
    case WM_VB_TRAY:
        if (LOWORD(lp) == WM_LBUTTONUP || LOWORD(lp) == WM_LBUTTONDBLCLK)
            vb_app_on_hotkey();
        else if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU)
            show_tray_menu();
        return 0;

    case WM_HOTKEY:
        if (wp == VB_HOTKEY_ID) vb_app_on_hotkey();
        return 0;

    case WM_VB_RECO:
        vb_local_handle_event();
        return 0;

    case WM_VB_RESULT:
        on_result((int)wp, (WCHAR *)lp);
        return 0;

    case WM_TIMER:
        vb_ui_tick();
        if (g_state == VB_ST_LISTENING && s_engine_used != VB_ENGINE_LOCAL) {
            vb_ui_set_level(vb_audio_level());
            if (g_cfg.stop_on_silence && vb_audio_speech_seen()) {
                ULONGLONG last = vb_audio_last_voice_ms();
                if (last && vb_now_ms() - last > (ULONGLONG)g_cfg.silence_ms)
                    vb_app_stop_and_transcribe();
            }
            if (vb_now_ms() - vb_audio_start_ms() > (ULONGLONG)g_cfg.max_record_ms)
                vb_app_stop_and_transcribe();
        }
        return 0;

    case MM_WIM_DATA:
        vb_audio_on_data((WAVEHDR *)lp);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdline, int show)
{
    WNDCLASSEXW wc;
    MSG msg;
    HANDLE mtx;
    (void)hPrev; (void)cmdline; (void)show;

    g_hinst = hInst;

    mtx = CreateMutexW(NULL, TRUE, L"Local\\VoiceBar.SingleInstance");
    if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"VoiceBar از قبل در حال اجراست (کنار ساعت ویندوز).",
                    VB_APP_NAME, MB_ICONINFORMATION | MB_OK);
        return 0;
    }

    SetProcessDPIAware();
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = main_proc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = VB_CLASS_MAIN;
    if (!RegisterClassExW(&wc)) return 1;

    g_hwnd_main = CreateWindowExW(0, VB_CLASS_MAIN, VB_APP_NAME, WS_OVERLAPPED,
                                  0, 0, 0, 0, NULL, NULL, hInst, NULL);
    if (!g_hwnd_main) return 1;

    vb_log(L"---- %s %s starting ----", VB_APP_NAME, VB_VERSION);
    {
        WCHAR exe[MAX_PATH], ini[MAX_PATH];
        vb_exe_dir(exe, MAX_PATH);
        _snwprintf(ini, MAX_PATH, L"%s\\VoiceBar.ini", exe);
        s_first_run = (GetFileAttributesW(ini) == INVALID_FILE_ATTRIBUTES);
    }
    vb_config_load();

    if (!vb_ui_create()) {
        MessageBoxW(NULL, L"ساخت رابط کاربری ناموفق بود.", VB_APP_NAME, MB_ICONERROR | MB_OK);
        return 1;
    }

    s_icon = vb_ui_make_icon();
    s_msg_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    tray_add();

    s_hotkey_ok = RegisterHotKey(g_hwnd_main, VB_HOTKEY_ID,
                                 g_cfg.hotkey_mods | MOD_NOREPEAT, g_cfg.hotkey_vk) ? 1 : 0;
    if (!s_hotkey_ok) {
        WCHAR m[512];
        _snwprintf(m, 500, L"کلید میان‌بر %s توسط برنامه دیگری گرفته شده است.\n"
                           L"مقدار hotkey= را در فایل VoiceBar.ini عوض کنید و از منوی کنار ساعت، "
                           L"«بارگذاری مجدد تنظیمات» را بزنید.", g_cfg.hotkey);
        vb_log(L"hotkey registration failed for %s", g_cfg.hotkey);
        MessageBoxW(NULL, m, VB_APP_NAME, MB_ICONWARNING | MB_OK);
    }

    SetTimer(g_hwnd_main, 1, 40, NULL);
    vb_log(L"ready (hotkey=%s engine=%d)", g_cfg.hotkey, effective_engine());

    if (s_first_run) {
        WCHAR m[1024];
        _snwprintf(m, 1000,
            L"VoiceBar آماده است.\n\n"
            L"۱) هر جایی که هستید %s را بزنید تا نوار پایین صفحه باز شود.\n"
            L"۲) صحبت کنید؛ متن به‌صورت خودکار در کادر نوشته می‌شود.\n"
            L"۳) با Enter متن در برنامه فعال چسبانده می‌شود و با Esc لغو می‌شود.\n\n"
            L"برای فارسی، در فایل VoiceBar.ini مقدار api_key را پر کنید "
            L"(OpenAI یا هر سرور سازگار) یا در بخش [command] مسیر whisper.cpp را بگذارید.\n\n"
            L"تنظیمات: %s\nگزارش: VoiceBar.log",
            g_cfg.hotkey, g_cfg.ini_path);
        MessageBoxW(NULL, m, L"VoiceBar", MB_ICONINFORMATION | MB_OK);
        vb_ui_show();
        vb_ui_set_status(L"آماده — کلید میان‌بر را بزنید و صحبت کنید");
    }

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (!s_quitting) {
        vb_audio_stop();
        vb_local_shutdown();
        tray_remove();
    }
    CoUninitialize();
    return 0;
}
