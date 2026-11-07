/* app.h - shared declarations for VoiceBar. */
#ifndef VB_APP_H
#define VB_APP_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>
#include <objbase.h>
#include <mmsystem.h>
#include <stdint.h>

#define VB_APP_NAME    L"VoiceBar"
#define VB_VERSION     L"1.0.0"
#define VB_CLASS_MAIN  L"VoiceBarMainWindow"
#define VB_CLASS_BAR   L"VoiceBarBarWindow"

/* messages posted to the main (invisible) window */
#define WM_VB_RECO     (WM_APP + 11)  /* SAPI recognition event            */
#define WM_VB_RESULT   (WM_APP + 12)  /* wParam 0=ok 1=error, lParam=WCHAR* */
#define WM_VB_TRAY     (WM_APP + 13)
#define WM_VB_TIMER    (WM_APP + 14)

#define VB_TRAY_ID     1

/* tray menu commands */
#define VB_CMD_SHOW     101
#define VB_CMD_SETTINGS 102
#define VB_CMD_RELOAD   103
#define VB_CMD_EXIT     104
#define VB_CMD_AUTOSTART 105

enum { VB_ENGINE_AUTO = 0, VB_ENGINE_LOCAL, VB_ENGINE_SERVER, VB_ENGINE_COMMAND };
enum { VB_ST_IDLE = 0, VB_ST_LISTENING, VB_ST_BUSY };

typedef struct {
    WCHAR ini_path[MAX_PATH];
    WCHAR hotkey[64];
    UINT  hotkey_mods;
    UINT  hotkey_vk;
    WCHAR language[16];
    int   engine;
    int   auto_paste;
    int   auto_accept;
    int   stop_on_silence;
    int   silence_ms;
    int   max_record_ms;
    WCHAR base_url[512];
    WCHAR api_key[512];
    WCHAR model[128];
    WCHAR prompt[256];
    WCHAR command[1024];
} VbConfig;

extern VbConfig  g_cfg;
extern HINSTANCE g_hinst;
extern HWND      g_hwnd_main;
extern int       g_state;

/* ---- main.c ---- */
void vb_app_on_hotkey(void);
void vb_app_start_listen(void);
void vb_app_stop_and_transcribe(void);
void vb_app_accept(void);
void vb_app_cancel(void);
void vb_app_on_enter(void);
void vb_app_on_escape(void);
void vb_app_reload_config(void);
void vb_app_quit(void);

/* ---- util.c ---- */
void      vb_log(const WCHAR *fmt, ...);
WCHAR    *vb_wcsdup(const WCHAR *s);
void      vb_exe_dir(WCHAR *out, int cch);
ULONGLONG vb_now_ms(void);
BOOL      vb_set_clipboard_text(HWND owner, const WCHAR *text);
void      vb_send_paste(void);
WCHAR    *vb_utf8_to_wide_alloc(const char *s);   /* NULL safe */
void      vb_ini_get(const WCHAR *file, const WCHAR *section, const WCHAR *key,
                     const WCHAR *def, WCHAR *out, DWORD cch);
int       vb_ini_get_int(const WCHAR *file, const WCHAR *section, const WCHAR *key, int def);

/* ---- config.c ---- */
void vb_config_defaults(void);
int  vb_config_load(void);
void vb_config_write_default(const WCHAR *path);
void vb_config_reload(void);
int  vb_hotkey_parse(const WCHAR *spec, UINT *mods, UINT *vk);

/* ---- ui.c ---- */
int  vb_ui_create(void);
void vb_ui_show(void);
void vb_ui_hide(void);
int  vb_ui_visible(void);
void vb_ui_set_status(const WCHAR *s);
void vb_ui_set_status_error(const WCHAR *s);
void vb_ui_set_text(const WCHAR *s);
void vb_ui_append_text(const WCHAR *s);
void vb_ui_get_text(WCHAR *out, int cch);
void vb_ui_set_level(int level);
void vb_ui_reset(void);
void vb_ui_tick(void);
HICON vb_ui_make_icon(void);

/* ---- audio.c ---- */
int       vb_audio_start(HWND notify);
void      vb_audio_stop(void);
void      vb_audio_on_data(WAVEHDR *hdr);
int       vb_audio_level(void);
ULONGLONG vb_audio_last_voice_ms(void);
int       vb_audio_speech_seen(void);
ULONGLONG vb_audio_start_ms(void);
int       vb_audio_active(void);
BYTE     *vb_audio_take_wav(DWORD *out_len);

/* ---- engines ---- */
void vb_server_transcribe(BYTE *wav, DWORD len);
void vb_command_transcribe(BYTE *wav, DWORD len);
int  vb_local_start(void);
void vb_local_stop(void);
void vb_local_handle_event(void);
void vb_local_shutdown(void);

#endif /* VB_APP_H */
