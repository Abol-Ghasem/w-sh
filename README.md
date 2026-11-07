# VoiceBar — تبدیل صدا به متن در هر جای ویندوز

یک برنامهٔ کوچک و تک‌فایلی برای ویندوز: هر جا که هستید یک کلید میان‌بر می‌زنید، نوار باریکی **پایین صفحه** باز می‌شود، صحبت می‌کنید و متن به‌صورت خودکار در همان کادر نوشته می‌شود. با `Enter` متن کپی و در برنامه‌ای که قبل از باز شدن نوار فعال بود چسبانده می‌شود، با `Esc` همه‌چیز لغو می‌شود.

| | |
|---|---|
| **فایل اجرایی** | [`VoiceBar.exe`](VoiceBar.exe) — همین‌جا در ریشهٔ مخزن (بدون نیاز به نصب، بدون DLL جانبی) |
| **سورس** | [`src/`](src) — C خالص + Win32 API |
| **حجم** | حدود ۱۱۴ کیلوبایت |
| **ویندوز** | Windows 8.1 / 10 / 11 — ۶۴ بیتی |

---

## ۱) شروع سریع

1. فایل `VoiceBar.exe` را دانلود و در یک پوشهٔ ثابت (مثلاً `C:\Tools\VoiceBar`) بگذارید.
2. روی آن دوبار کلیک کنید. اولین اجرا فایل `VoiceBar.ini` را کنار خودش می‌سازد و یک پیام راهنما نشان می‌دهد.
3. برنامه کنار ساعت ویندوز (سینی سیستم) می‌نشیند. آیکونش را ببینید؛ اگر پنهان بود از فلش کنار ساعت بیرونش بکشید.
4. حالا هر جا که هستید **`Ctrl + Alt + Space`** را بزنید.

| کلید | کار |
|---|---|
| `Ctrl+Alt+Space` | باز کردن نوار / پایان دادن به ضبط |
| `Enter` | پایان ضبط و تبدیل، سپس چسباندن متن (در حالت آماده: چسباندن) |
| `Esc` | لغو و بستن نوار |
| کلیک راست روی آیکون سینی | نمایش نوار، ویرایش تنظیمات، بارگذاری مجدد تنظیمات، مشاهدهٔ گزارش، خروج |

> در حالت پیش‌فرض، وقتی صحبتتان تمام می‌شود و حدود ۱٫۲ ثانیه سکوت می‌کنید، ضبط **خودکار** تمام می‌شود و متن ساخته می‌شود؛ نیازی به زدن Enter نیست (`stop_on_silence=1`).

---

## ۲) موتورهای تبدیل گفتار به متن

برنامه سه موتور دارد؛ با `engine=` در `VoiceBar.ini` انتخاب می‌شود:

| موتور | توضیح | فارسی؟ | آفلاین؟ |
|---|---|---|---|
| `server` | هر سرویس سازگار با OpenAI در مسیر `/audio/transcriptions` (OpenAI، Groq، LocalAI، faster-whisper-server، …) | ✅ | ❌ |
| `command` | صدا در یک فایل wav موقت ذخیره می‌شود و یک برنامهٔ دلخواه روی آن اجرا می‌شود (مثلاً whisper.cpp یا faster-whisper) | ✅ | ✅ |
| `local` | تشخیص گفتار خود ویندوز (SAPI 5) — بدون هیچ تنظیمی کار می‌کند | ❌ | ✅ |

مقدار `auto` (پیش‌فرض) این‌طور انتخاب می‌کند: اگر `api_key` پر باشد `server`، وگرنه اگر بخش `[command]` پر باشد `command`، وگرنه `local`.

> ⚠️ **مهم برای فارسی:** موتور آفلاین ویندوز فقط زبان‌هایی را می‌شناسد که بستهٔ Speech آن‌ها نصب شده باشد (انگلیسی، آلمانی، فرانسه، اسپانیایی، ژاپنی، چینی…) و **فارسی را پشتیبانی نمی‌کند**. برای فارسی از `server` یا `command` استفاده کنید.

### الف) فارسی با سرویس Whisper (ساده‌ترین راه با کیفیت خوب)

در `VoiceBar.ini`:

```ini
[general]
engine=auto
language=fa

[server]
base_url=https://api.openai.com/v1
api_key=sk-...کلید-خودتان...
model=whisper-1
```

یا با Groq (سریع و ارزان):

```ini
[server]
base_url=https://api.groq.com/openai/v1
api_key=gsk_...
model=whisper-large-v3-turbo
```

اگر سرور محلی روی کامپیوتر خودتان دارید (مثلاً faster-whisper-server):

```ini
[server]
base_url=http://127.0.0.1:8000/v1
api_key=
model=Systran/faster-whisper-small
```

بعد از ویرایش فایل، از منوی آیکون سینی **«بارگذاری مجدد تنظیمات»** را بزنید (نیازی به بستن برنامه نیست).

### ب) فارسی کاملاً آفلاین با whisper.cpp

۱) از [whisper.cpp](https://github.com/ggml-org/whisper.cpp/releases) فایل اجرایی ویندوز و یک مدل چندزبانه (مثلاً `ggml-small.bin` یا `ggml-medium.bin`) را دانلود کنید.
۲) در `VoiceBar.ini`:

```ini
[general]
engine=command

[command]
line=C:\Tools\whisper\whisper-cli.exe -m C:\Tools\whisper\ggml-small.bin -l fa -nt -f "%wav%"
```

`%wav%` با مسیر فایل صدای ضبط‌شده جایگزین می‌شود و هر چیزی که برنامه در خروجی استاندارد (stdout) چاپ کند به‌عنوان متن استفاده می‌شود. پرچم `-nt` باعث می‌شود whisper.cpp فقط متن خالص چاپ کند (بدون زمان‌بندی).

### ج) موتور آفلاین ویندوز (`local`)

هیچ تنظیمی لازم نیست؛ از زبان پیش‌فرض تشخیص گفتار ویندوز استفاده می‌کند و متن به‌صورت زنده و کلمه‌به‌کلمه در کادر نوشته می‌شود. برای فارسی مناسب نیست.

---

## ۳) تنظیمات (`VoiceBar.ini`)

فایل تنظیمات همیشه کنار `VoiceBar.exe` است و در اولین اجرا ساخته می‌شود.

| کلید | پیش‌فرض | معنی |
|---|---|---|
| `hotkey` | `CTRL+ALT+SPACE` | کلید میان‌بر سراسری. ترکیب `CTRL/ALT/SHIFT/WIN` با یک کلید: `A-Z`, `0-9`, `F1-F24`, `SPACE`, `ENTER`, `TAB`, `ESC`, `INSERT`, `HOME`, `END`, `;`, `-`, `=`, `,`, `.` |
| `engine` | `auto` | `auto` / `server` / `command` / `local` |
| `language` | خالی | زبان برای موتور `server`؛ `fa` یا `en` یا خالی برای تشخیص خودکار |
| `auto_paste` | `1` | متن در پنجره‌ای که قبل از باز شدن نوار فعال بود چسبانده شود |
| `auto_accept` | `0` | اگر `1` باشد، بعد از ساخت متن بدون زدن Enter مستقیم می‌چسباند |
| `stop_on_silence` | `1` | پایان خودکار ضبط در سکوت |
| `silence_ms` | `1200` | مدت سکوت (میلی‌ثانیه) برای پایان خودکار |
| `max_record_ms` | `120000` | حداکثر طول یک ضبط (۲ دقیقه) |
| `base_url` | `https://api.openai.com/v1` | آدرس سرویس سازگار با OpenAI |
| `api_key` | خالی | کلید API؛ اگر خالی باشد هدر `Authorization` فرستاده نمی‌شود |
| `model` | `whisper-1` | نام مدل |
| `prompt` | خالی | راهنمای واژگان (اسم‌های خاص، اصطلاحات) برای موتور `server` |
| `line` | خالی | دستور اجرایی موتور `command` (`%wav%` = مسیر فایل صدا) |

فایل `ini` را می‌توانید با Notepad ویرایش کنید (از منوی آیکون سینی: «تنظیمات»). فایل با کدگذاری UTF-16 ذخیره می‌شود تا متن فارسی هم بدون مشکل ذخیره شود.

---

## ۴) اجرای خودکار با ویندوز

`Win + R` را بزنید، عبارت `shell:startup` را وارد کنید، و در پوشه‌ای که باز می‌شود یک «میان‌بر» (Shortcut) از `VoiceBar.exe` بسازید.

---

## ۵) ساخت از سورس

نیازی به Visual Studio نیست؛ با [Zig](https://ziglang.org/download/) می‌توان از لینوکس/مک/ویندوز یک exe واقعی ویندوزی ساخت (همان روشی که فایل منتشرشده با آن ساخته شده است):

```bash
# لینوکس / مک (یا ویندوز بعد از افزودن zig به PATH)
./build.sh              # ساخت VoiceBar.exe
./build.sh test         # اجرای تست‌های واحد منطق خالص
./build.sh debug        # نسخهٔ اشکال‌زدایی با کنسول
```

در ویندوز:

```bat
build.bat               :: با zig
build.bat msvc          :: از داخل "x64 Native Tools Command Prompt" با MSVC
build.bat test          :: تست‌ها
```

تست‌ها (`tests/test_core.c`) منطق مستقل از ویندوز را می‌سنجند: تبدیل UTF-8/UTF-16، ساخت هدر WAV، پارس پاسخ JSON سرویس و ساخت بدنهٔ `multipart/form-data`.

---

## ۶) ساختار پروژه

```
VoiceBar.exe          فایل اجرایی ساخته‌شده (ویندوز ۶۴ بیتی)
VoiceBar.ini          فایل تنظیمات پیش‌فرض (در اولین اجرا هم ساخته می‌شود)
build.sh / build.bat  اسکریپت ساخت (zig یا MSVC)
src/
  main.c              نقطهٔ شروع، کلید میان‌بر، آیکون سینی، ماشین حالت
  ui.c                نوار پایین صفحه: رسم، کادر متن، سطح صدا
  audio.c             ضبط میکروفون با waveIn و ساخت بافر WAV
  stt_server.c        ارسال WAV به سرویس سازگار با OpenAI (WinHTTP)
  stt_cmd.c           اجرای برنامهٔ خارجی روی فایل WAV
  stt_local.c         تشخیص گفتار آفلاین ویندوز (SAPI 5)
  config.c            خواندن/نوشتن ini و تجزیهٔ کلید میان‌بر
  util.c              کلیپ‌بورد، چسباندن، لاگ، مسیرها
  core.c / core.h     منطق مستقل از پلتفرم (JSON، WAV، UTF، multipart)
tests/test_core.c     تست‌های واحد core
```

---

## ۷) عیب‌یابی

| مشکل | راه‌حل |
|---|---|
| ویندوز هنگام اجرا هشدار «Windows protected your PC» می‌دهد | فایل امضای دیجیتال ندارد: `More info` → `Run anyway` |
| کلید میان‌بر کار نمی‌کند | در `VoiceBar.ini` مقدار `hotkey` را عوض کنید (مثلاً `CTRL+ALT+D`) و از منوی سینی «بارگذاری مجدد تنظیمات» را بزنید. برنامه هم اگر ترکیب اشغال باشد در شروع پیام می‌دهد |
| هیچ متنی ساخته نمی‌شود | فایل `VoiceBar.log` کنار برنامه را ببینید؛ خطای شبکه/کلید/میکروفون آنجا ثبت می‌شود |
| میکروفون اجازه ندارد | Settings → Privacy → Microphone → «اجازهٔ دسترسی برنامه‌های دسکتاپ» روشن باشد |
| متن در برنامهٔ مقصد چسبانده نمی‌شود | بعضی برنامه‌ها (برخی ترمینال‌ها یا برنامه‌های Run as Administrator) چسباندن خودکار را قبول نمی‌کنند؛ متن در کلیپ‌بورد هست، با `Ctrl+V` بچسبانید. اگر برنامهٔ مقصد با دسترسی بالاتر اجرا شده باشد، VoiceBar هم باید با همان دسترسی اجرا شود |
| پشت پروکسی هستم | اتصال‌ها از WinHTTP استفاده می‌کنند و پروکسی سیستم را خودکار می‌گیرند |
| موتور `local` فارسی نمی‌فهمد | طبیعی است؛ از `server` یا `command` استفاده کنید |

---

## English summary

**VoiceBar** is a tiny single-file Windows utility (C + Win32 API, built with
`zig cc -target x86_64-windows-gnu`, ~114 KB, no runtime dependencies).
Press a global hotkey (`Ctrl+Alt+Space` by default) and a slim, always-on-top bar
slides up at the bottom of the screen. Your microphone is recorded, speech is
turned into text inside that bar, and `Enter` copies the text and pastes it into
the window that was focused before — `Esc` cancels.

Three interchangeable back ends:

* `server` — POSTs a 16 kHz mono WAV to any OpenAI-compatible
  `/audio/transcriptions` endpoint via WinHTTP (works for Persian).
* `command` — runs any external CLI (e.g. whisper.cpp) on the recorded WAV and
  takes stdout as the text (offline, works for Persian).
* `local` — offline Windows Speech Recognition through SAPI 5 (no Persian).

Everything is configurable from the `VoiceBar.ini` next to the executable
(hotkey, engine, language, auto-paste, silence detection, API endpoint/key/model).
A tray icon exposes *show*, *edit settings*, *reload settings*, *open log* and *quit*.

`src/core.c` (UTF-8/UTF-16 conversion, WAV header, JSON field extraction,
multipart body builder) is platform independent and covered by
`tests/test_core.c`; run it with `./build.sh test`.

### Known limits

* The executable is unsigned, so SmartScreen warns on first run.
* The `server` engine records the whole utterance and uploads it afterwards
  (no live streaming); the `local` engine is the only live-dictation mode.
* One recording is capped at `max_record_ms` (2 minutes by default).

---

ساخته‌شده با C و Win32 API. آزاد برای استفاده و تغییر.
