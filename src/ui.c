/* ui.c - the floating bar that appears at the bottom of the screen. */
#include "app.h"
#include "core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define VB_BAR_W   680
#define VB_BAR_H   118

#define C_BG        RGB(22, 24, 30)
#define C_SEP       RGB(40, 44, 54)
#define C_EDITBG    RGB(30, 33, 41)
#define C_TEXT      RGB(236, 239, 245)
#define C_DIM       RGB(138, 147, 163)
#define C_ACCENT    RGB(84, 158, 255)
#define C_ACCENT2   RGB(52, 106, 178)
#define C_LEVEL     RGB(120, 200, 255)
#define C_ERR       RGB(255, 130, 120)

static HWND    s_bar, s_edit;
static HFONT   s_font_text, s_font_small;
static HBRUSH  s_brush_bg, s_brush_edit;
static WNDPROC s_edit_old_proc;
static WCHAR   s_status[256];
static int     s_level, s_level_smooth;
static int     s_visible;
static int     s_dpi = 96;
static int     s_anim;
static int     s_status_error;

static int S(int v) { return MulDiv(v, s_dpi, 96); }

static void set_status_color(int is_err) { s_status_error = is_err; }

/* ------------------------------------------------------------------- icon */

static unsigned char clamp8(int v) { return (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

static void px(unsigned char *p, int *rgb, int a)
{
    p[0] = clamp8((rgb[2] * a + p[0] * (255 - a)) / 255);  /* B */
    p[1] = clamp8((rgb[1] * a + p[1] * (255 - a)) / 255);  /* G */
    p[2] = clamp8((rgb[0] * a + p[2] * (255 - a)) / 255);  /* R */
    p[3] = 255;
}

HICON vb_ui_make_icon(void)
{
    const int n = 32;
    BITMAPINFO bi;
    void *bits = NULL;
    unsigned char zero_mask[32 * 32 / 8];
    HBITMAP color, mask;
    ICONINFO ii;
    int x, y;
    int accent[3] = { 84, 158, 255 };
    int white[3] = { 255, 255, 255 };

    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = n;
    bi.bmiHeader.biHeight = -n;   /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    color = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!color || !bits) return LoadIconW(NULL, IDI_APPLICATION);

    for (y = 0; y < n; y++) {
        unsigned char *row = (unsigned char *)bits + y * n * 4;
        for (x = 0; x < n; x++) {
            unsigned char *p = row + x * 4;
            double dx = x - 15.5, dy = y - 15.5;
            double d = sqrt(dx * dx + dy * dy);
            int a = (int)((15.3 - d) * 255.0);
            p[0] = p[1] = p[2] = 0;
            p[3] = 0;
            if (a > 0) {
                p[0] = (unsigned char)accent[2];
                p[1] = (unsigned char)accent[1];
                p[2] = (unsigned char)accent[0];
                p[3] = clamp8(a);
            }
            /* microphone body: rounded capsule x 12..19, y 6..19 */
            if (x >= 11 && x <= 20 && y >= 5 && y <= 19) {
                int inx = (x >= 13 && x <= 18) || (y >= 7 && y <= 17);
                if (inx) px(p, white, 255);
            }
            /* arc + stand */
            if ((y >= 19 && y <= 22) && ((x >= 8 && x <= 10) || (x >= 21 && x <= 23)))
                px(p, white, 200);
            if (y >= 23 && y <= 25 && x >= 15 && x <= 16) px(p, white, 255);
            if (y >= 25 && y <= 26 && x >= 11 && x <= 20) px(p, white, 255);
        }
    }

    ZeroMemory(zero_mask, sizeof(zero_mask));
    mask = CreateBitmap(n, n, 1, 1, zero_mask);
    ZeroMemory(&ii, sizeof(ii));
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    {
        HICON ic = CreateIconIndirect(&ii);
        DeleteObject(color);
        DeleteObject(mask);
        if (ic) return ic;
    }
    return LoadIconW(NULL, IDI_APPLICATION);
}

/* -------------------------------------------------------------- edit ctrl */

static LRESULT CALLBACK edit_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_KEYDOWN:
        if (w == VK_RETURN)  { vb_app_on_enter();  return 0; }
        if (w == VK_ESCAPE)  { vb_app_on_escape(); return 0; }
        if (w == VK_TAB)     return 0;
        break;
    case WM_CHAR:
        if (w == 13 || w == 10 || w == 27) return 0;
        break;
    case WM_ERASEBKGND:
        return 1;
    }
    return CallWindowProcW(s_edit_old_proc, h, m, w, l);
}

static void layout(HWND hwnd)
{
    RECT rc;
    int w, h;
    GetClientRect(hwnd, &rc);
    w = rc.right;
    h = rc.bottom;
    if (s_edit)
        MoveWindow(s_edit, S(72), S(16), w - S(72) - S(18), h - S(16) - S(44), TRUE);
    {
        HRGN rgn = CreateRoundRectRgn(0, 0, w + 1, h + 1, S(26), S(26));
        SetWindowRgn(hwnd, rgn, TRUE);
    }
}

static void draw_level_bars(HDC hdc)
{
    int i;
    int base = s_level_smooth;
    for (i = 0; i < 5; i++) {
        double ph = (double)s_anim * 0.32 + (double)i * 1.15;
        double wob = 0.42 + 0.58 * fabs(sin(ph));
        int hgt = S(3) + (int)((double)S(34) * (base / 100.0) * wob);
        int x = S(20) + i * S(9);
        int y = S(58) - hgt / 2;
        HBRUSH br = CreateSolidBrush(base > 45 ? C_LEVEL : C_ACCENT);
        RECT r;
        r.left = x; r.top = y; r.right = x + S(5); r.bottom = y + hgt;
        FillRect(hdc, &r, br);
        DeleteObject(br);
    }
}

static void paint_bar(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc, tr;
    HDC mem;
    HBITMAP bmp;
    HGDIOBJ old;

    GetClientRect(hwnd, &rc);
    mem = CreateCompatibleDC(hdc);
    bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    old = SelectObject(mem, bmp);

    FillRect(mem, &rc, s_brush_bg);

    draw_level_bars(mem);

    /* separator above the status line */
    tr.left = S(16);
    tr.right = rc.right - S(16);
    tr.top = rc.bottom - S(34);
    tr.bottom = tr.top + 1;
    {
        HBRUSH sep = CreateSolidBrush(C_SEP);
        FillRect(mem, &tr, sep);
        DeleteObject(sep);
    }

    SetBkMode(mem, TRANSPARENT);
    if (s_font_small) SelectObject(mem, s_font_small);
    SetTextColor(mem, s_status_error ? C_ERR : C_DIM);
    tr.left = S(18);
    tr.right = rc.right - S(220);
    tr.top = rc.bottom - S(29);
    tr.bottom = rc.bottom - S(6);
    DrawTextW(mem, s_status, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);

    SetTextColor(mem, C_DIM);
    tr.left = rc.right - S(240);
    tr.right = rc.right - S(18);
    DrawTextW(mem, L"Enter   تأیید        Esc   انصراف", -1, &tr,
              DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK bar_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        s_edit = CreateWindowExW(0, L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_LEFT,
                                 0, 0, 10, 10, hwnd, (HMENU)1001, g_hinst, NULL);
        if (s_edit) {
            SendMessageW(s_edit, WM_SETFONT, (WPARAM)s_font_text, TRUE);
            SendMessageW(s_edit, EM_SETLIMITTEXT, (WPARAM)64000, 0);
            SendMessageW(s_edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                         MAKELONG(S(2), S(2)));
            s_edit_old_proc = (WNDPROC)SetWindowLongPtrW(s_edit, GWLP_WNDPROC,
                                                         (LONG_PTR)edit_proc);
        }
        layout(hwnd);
        return 0;

    case WM_SIZE:
        layout(hwnd);
        return 0;

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        SetBkColor(dc, C_EDITBG);
        SetTextColor(dc, C_TEXT);
        return (LRESULT)s_brush_edit;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        paint_bar(hwnd);
        return 0;

    case WM_LBUTTONDOWN:
        if (s_edit) SetFocus(s_edit);
        return 0;

    case WM_CLOSE:
        vb_ui_hide();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------- api */

int vb_ui_create(void)
{
    WNDCLASSEXW wc;
    HDC dc;
    LOGFONTW lf;
    RECT probe;

    dc = GetDC(NULL);
    if (dc) {
        s_dpi = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(NULL, dc);
    }
    if (s_dpi <= 0) s_dpi = 96;

    s_brush_bg = CreateSolidBrush(C_BG);
    s_brush_edit = CreateSolidBrush(C_EDITBG);

    ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight = -S(17);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy(lf.lfFaceName, L"Segoe UI");
    s_font_text = CreateFontIndirectW(&lf);

    lf.lfHeight = -S(12);
    s_font_small = CreateFontIndirectW(&lf);

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = bar_proc;
    wc.hInstance = g_hinst;
    wc.hCursor = LoadCursorW(NULL, IDC_IBEAM);
    wc.lpszClassName = VB_CLASS_BAR;
    if (!RegisterClassExW(&wc)) return 0;

    probe.left = 0; probe.top = 0; probe.right = S(VB_BAR_W); probe.bottom = S(VB_BAR_H);

    s_bar = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                            VB_CLASS_BAR, VB_APP_NAME,
                            WS_POPUP,
                            0, 0, probe.right, probe.bottom,
                            NULL, NULL, g_hinst, NULL);
    if (!s_bar) return 0;
    return 1;
}

void vb_ui_show(void)
{
    POINT pt;
    MONITORINFO mi;
    HMONITOR mon;
    RECT rc;
    int w, h, x, y;

    if (!s_bar) return;
    w = S(VB_BAR_W);
    h = S(VB_BAR_H);

    GetCursorPos(&pt);
    mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rc, 0);
    } else {
        rc = mi.rcWork;
    }

    x = rc.left + ((rc.right - rc.left) - w) / 2;
    y = rc.bottom - h - S(48);
    if (y < rc.top) y = rc.top;

    SetWindowPos(s_bar, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowWindow(s_bar, SW_SHOWNOACTIVATE);
    SetForegroundWindow(s_bar);
    SetActiveWindow(s_bar);
    SetWindowPos(s_bar, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (s_edit) {
        int len = GetWindowTextLengthW(s_edit);
        SetFocus(s_edit);
        SendMessageW(s_edit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    }
    s_visible = 1;
    InvalidateRect(s_bar, NULL, FALSE);
}

void vb_ui_hide(void)
{
    if (!s_bar) return;
    ShowWindow(s_bar, SW_HIDE);
    s_visible = 0;
    s_level = s_level_smooth = 0;
}

int vb_ui_visible(void) { return s_visible; }

void vb_ui_set_status(const WCHAR *s)
{
    if (!s) s = L"";
    wcsncpy(s_status, s, 255);
    s_status[255] = 0;
    set_status_color(0);
    if (s_bar) InvalidateRect(s_bar, NULL, FALSE);
}

void vb_ui_set_status_error(const WCHAR *s)
{
    if (!s) s = L"";
    wcsncpy(s_status, s, 255);
    s_status[255] = 0;
    set_status_color(1);
    if (s_bar) InvalidateRect(s_bar, NULL, FALSE);
}

void vb_ui_reset(void)
{
    if (s_edit) SetWindowTextW(s_edit, L"");
    vb_ui_set_status(L"");
    s_anim = 0;
    s_level = s_level_smooth = 0;
}

void vb_ui_set_text(const WCHAR *s)
{
    if (s_edit) SetWindowTextW(s_edit, s ? s : L"");
}

void vb_ui_append_text(const WCHAR *s)
{
    int len;
    if (!s_edit || !s) return;
    if (!s[0]) return;
    len = GetWindowTextLengthW(s_edit);
    SendMessageW(s_edit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    if (len > 0) SendMessageW(s_edit, EM_REPLACESEL, FALSE, (LPARAM)L" ");
    SendMessageW(s_edit, EM_REPLACESEL, FALSE, (LPARAM)s);
    SendMessageW(s_edit, EM_SCROLLCARET, 0, 0);
}

void vb_ui_get_text(WCHAR *out, int cch)
{
    if (!out || cch <= 0) return;
    out[0] = 0;
    if (s_edit) GetWindowTextW(s_edit, out, cch);
}

void vb_ui_set_level(int level)
{
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    s_level = level;
}

void vb_ui_tick(void)
{
    if (!s_visible) return;
    s_anim++;
    if (s_level_smooth != s_level) {
        if (s_level > s_level_smooth) s_level_smooth = (s_level_smooth + s_level) / 2 + 1;
        else s_level_smooth = (s_level_smooth * 3 + s_level) / 4;
        if (s_level == 0 && s_level_smooth <= 2) s_level_smooth = 0;
    }
    if (s_level_smooth > 0 || (s_anim % 8) == 0)
        InvalidateRect(s_bar, NULL, FALSE);
}
