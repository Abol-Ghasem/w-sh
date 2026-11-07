/* test_core.c - native unit tests for the platform independent part.
 * Build:  gcc -O2 -Wall -Wextra tests/test_core.c src/core.c -o /tmp/test_core -lm */
#include "../src/core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check(int cond, const char *what)
{
    if (cond) {
        printf("  ok   %s\n", what);
    } else {
        printf("  FAIL %s\n", what);
        failures++;
    }
}

static void test_utf(void)
{
    size_t n = 0;
    uint16_t *u;
    char *b;
    const char *fa = "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85";   /* سلام */

    printf("utf\n");
    u = vb_utf8_to_utf16(fa, &n);
    check(u && n == 4, "utf8->utf16 length for Persian word");
    check(u && u[0] == 0x0633 && u[3] == 0x0645, "utf8->utf16 code points");
    b = vb_utf16_to_utf8(u, n);
    check(b && strcmp(b, fa) == 0, "utf16->utf8 round trip");
    free(b);
    free(u);

    /* emoji (surrogate pair) round trip */
    u = vb_utf8_to_utf16("\xF0\x9F\x8E\x99", &n);
    check(u && n == 2, "surrogate pair is two utf16 units (mic emoji)");
    b = vb_utf16_to_utf8(u, n);
    check(b && strcmp(b, "\xF0\x9F\x8E\x99") == 0, "surrogate pair round trip");
    free(b);
    free(u);
}

static void test_wav(void)
{
    unsigned char h[44];
    uint32_t total;

    printf("wav\n");
    memset(h, 0xAA, sizeof(h));
    total = vb_wav_header(h, 32000, 1, 16000, 16);
    check(total == 32044, "total size = 44 + pcm");
    check(memcmp(h, "RIFF", 4) == 0, "RIFF tag");
    check(h[4] == (32036 & 0xFF) && h[5] == ((32036 >> 8) & 0xFF), "RIFF size = 36+pcm");
    check(memcmp(h + 8, "WAVEfmt ", 8) == 0, "WAVE + fmt chunk");
    check(h[20] == 1 && h[21] == 0, "PCM format tag");
    check(h[22] == 1, "mono");
    check(h[24] == 0x80 && h[25] == 0x3E, "16000 Hz little endian");
    check(h[28] == 0x00 && h[29] == 0x7D, "byte rate 32000");
    check(h[32] == 2, "block align 2");
    check(h[34] == 16, "16 bits per sample");
    check(memcmp(h + 36, "data", 4) == 0, "data tag");
    check(h[40] == 0x00 && h[41] == 0x7D, "data size 32000");
}

static void test_json(void)
{
    char *s;

    printf("json\n");
    s = vb_json_get_string("{\"text\":\"hello world\"}", "text");
    check(s && strcmp(s, "hello world") == 0, "plain text field");
    free(s);

    s = vb_json_get_string("{\n  \"task\": \"transcribe\",\n  \"text\": \"hi\\nthere\"\n}", "text");
    check(s && strcmp(s, "hi\nthere") == 0, "escaped newline");
    free(s);

    s = vb_json_get_string("{\"text\":\"\\u0633\\u0644\\u0627\\u0645\"}", "text");
    check(s && strcmp(s, "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85") == 0, "\\u escapes to utf8 (Persian)");
    free(s);

    s = vb_json_get_string("{\"text\":\"\\ud83c\\udf99 mic\"}", "text");
    check(s && strcmp(s, "\xF0\x9F\x8E\x99 mic") == 0, "surrogate escape pair");
    free(s);

    s = vb_json_get_string("{\"error\":{\"message\":\"bad key \\\"x\\\"\"}}", "message");
    check(s && strcmp(s, "bad key \"x\"") == 0, "nested error message with quotes");
    free(s);

    s = vb_json_get_string("{\"task\":\"transcribe\"}", "text");
    check(s == NULL, "missing key returns NULL");
    s = vb_json_get_string("[1,2]", "text");
    check(s == NULL, "non object returns NULL");
}

static void test_multipart(void)
{
    const char *names[2] = { "model", "language" };
    const char *values[2] = { "whisper-1", "fa" };
    VbMultipart mp;
    uint32_t len = 0;
    unsigned char *body;
    char *s;

    printf("multipart\n");
    memset(&mp, 0, sizeof(mp));
    mp.file_field = "file";
    mp.file_name = "audio.wav";
    mp.file_type = "audio/wav";
    mp.file_data = (const uint8_t *)"RIFFDATA";
    mp.file_len = 8;
    mp.names = names;
    mp.values = values;
    mp.n = 2;

    body = vb_multipart_body("BOUNDARY", &mp, &len);
    check(body && len > 0, "body built");
    s = (char *)malloc(len + 1);
    memcpy(s, body, len);
    s[len] = 0;
    check(strstr(s, "--BOUNDARY\r\n") == s, "starts with boundary");
    check(strstr(s, "name=\"model\"\r\n\r\nwhisper-1\r\n") != NULL, "text field model");
    check(strstr(s, "name=\"language\"\r\n\r\nfa\r\n") != NULL, "text field language");
    check(strstr(s, "name=\"file\"; filename=\"audio.wav\"\r\n") != NULL, "file field header");
    check(strstr(s, "Content-Type: audio/wav\r\n\r\nRIFFDATA\r\n") != NULL, "file payload");
    {
        const char *tail = "\r\n--BOUNDARY--\r\n";
        size_t n = strlen(s), t = strlen(tail);
        check(n >= t && strcmp(s + n - t, tail) == 0, "terminator");
    }
    free(s);
    free(body);
}

int main(void)
{
    printf("sizeof checks: uint16=%d\n", (int)sizeof(uint16_t));
    test_utf();
    test_wav();
    test_json();
    test_multipart();
    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
