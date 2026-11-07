/* stt_local.c - offline dictation through the Windows SAPI 5 recognizer.
 *
 * The shared recognizer is used, which reads from the default recording device
 * of the system. Windows only ships recognizers for a handful of languages
 * (en-US, en-GB, de-DE, fr-FR, es-ES, ja-JP, zh-CN ...), Persian is NOT among
 * them, so for Persian use the "server" or "command" engine. */
#define INITGUID
#include "app.h"
#include "core.h"

#include <sapi.h>
#include <stdlib.h>

static ISpRecognizer   *s_reco;
static ISpRecoContext  *s_ctx;
static ISpRecoGrammar  *s_gram;
static int              s_failed;

static void release_all(void)
{
    if (s_gram) { s_gram->lpVtbl->Release(s_gram); s_gram = NULL; }
    if (s_ctx)  { s_ctx->lpVtbl->Release(s_ctx);  s_ctx = NULL; }
    if (s_reco) { s_reco->lpVtbl->Release(s_reco); s_reco = NULL; }
}

static int local_init(void)
{
    HRESULT hr;

    if (s_ctx) return 1;
    if (s_failed) return 0;

    hr = CoCreateInstance(&CLSID_SpSharedRecognizer, NULL, CLSCTX_ALL,
                          &IID_ISpRecognizer, (void **)&s_reco);
    if (FAILED(hr) || !s_reco) {
        vb_log(L"local: CoCreateInstance(SpSharedRecognizer) failed 0x%08lX", (unsigned long)hr);
        s_failed = 1;
        return 0;
    }

    hr = s_reco->lpVtbl->CreateRecoContext(s_reco, &s_ctx);
    if (FAILED(hr) || !s_ctx) {
        vb_log(L"local: CreateRecoContext failed 0x%08lX", (unsigned long)hr);
        release_all();
        s_failed = 1;
        return 0;
    }

    s_ctx->lpVtbl->SetNotifyWindowMessage(s_ctx, g_hwnd_main, WM_VB_RECO, 0, 0);
    s_ctx->lpVtbl->SetInterest(s_ctx, SPFEI(SPEI_RECOGNITION), SPFEI(SPEI_RECOGNITION));

    hr = s_ctx->lpVtbl->CreateGrammar(s_ctx, 1, &s_gram);
    if (FAILED(hr) || !s_gram) {
        vb_log(L"local: CreateGrammar failed 0x%08lX", (unsigned long)hr);
        release_all();
        s_failed = 1;
        return 0;
    }

    hr = s_gram->lpVtbl->LoadDictation(s_gram, NULL, SPLO_DYNAMIC);
    if (FAILED(hr)) {
        vb_log(L"local: LoadDictation failed 0x%08lX", (unsigned long)hr);
        release_all();
        s_failed = 1;
        return 0;
    }

    vb_log(L"local: SAPI recognizer ready");
    return 1;
}

int vb_local_start(void)
{
    HRESULT hr;
    if (!local_init()) return 0;

    hr = s_reco->lpVtbl->SetRecoState(s_reco, SPRST_ACTIVE);
    if (FAILED(hr)) vb_log(L"local: SetRecoState failed 0x%08lX", (unsigned long)hr);

    hr = s_gram->lpVtbl->SetDictationState(s_gram, SPRS_ACTIVE);
    if (FAILED(hr)) {
        vb_log(L"local: SetDictationState failed 0x%08lX", (unsigned long)hr);
        return 0;
    }
    return 1;
}

void vb_local_stop(void)
{
    if (!s_gram) return;
    s_gram->lpVtbl->SetDictationState(s_gram, SPRS_INACTIVE);
}

void vb_local_shutdown(void)
{
    if (s_reco) s_reco->lpVtbl->SetRecoState(s_reco, SPRST_INACTIVE);
    release_all();
}

void vb_local_handle_event(void)
{
    if (!s_ctx) return;

    for (;;) {
        SPEVENT ev;
        ULONG fetched = 0;
        HRESULT hr;
        ZeroMemory(&ev, sizeof(ev));
        hr = s_ctx->lpVtbl->GetEvents(s_ctx, 1, &ev, &fetched);
        if (FAILED(hr) || fetched == 0) break;

        if (ev.eEventId == SPEI_RECOGNITION && ev.lParam) {
            ISpRecoResult *res = (ISpRecoResult *)ev.lParam;
            WCHAR *text = NULL;
            hr = res->lpVtbl->GetText(res, SP_GETWHOLEPHRASE, SP_GETWHOLEPHRASE, TRUE, &text, NULL);
            if (SUCCEEDED(hr) && text && text[0]) {
                vb_ui_append_text(text);
                CoTaskMemFree(text);
            } else if (text) {
                CoTaskMemFree(text);
            }
            res->lpVtbl->Release(res);
        } else if (ev.lParam && ev.elParamType == SPET_LPARAM_IS_OBJECT) {
            IUnknown *unk = (IUnknown *)ev.lParam;
            unk->lpVtbl->Release(unk);
        }
    }
}
