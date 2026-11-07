/* core.c - platform independent helpers. */
#include "core.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ utf-16 */

uint16_t *vb_utf8_to_utf16(const char *s, size_t *out_len)
{
    size_t n = 0, cap = 16, i = 0;
    uint16_t *out;
    if (!s) return NULL;
    out = (uint16_t *)malloc(cap * sizeof(uint16_t));
    if (!out) return NULL;
    while (s[i]) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp;
        int extra;
        if (c < 0x80)        { cp = c;        extra = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else { i++; cp = 0xFFFD; extra = -1; }
        i++;
        if (extra > 0) {
            int k;
            for (k = 0; k < extra; k++) {
                unsigned char cc = (unsigned char)s[i];
                if ((cc & 0xC0) != 0x80) { cp = 0xFFFD; break; }
                cp = (cp << 6) | (unsigned)(cc & 0x3F);
                i++;
            }
        }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            if (n + 2 >= cap) { cap *= 2; out = (uint16_t *)realloc(out, cap * sizeof(uint16_t)); if (!out) return NULL; }
            out[n++] = (uint16_t)(0xD800 + (cp >> 10));
            out[n++] = (uint16_t)(0xDC00 + (cp & 0x3FF));
        } else {
            if (n + 1 >= cap) { cap *= 2; out = (uint16_t *)realloc(out, cap * sizeof(uint16_t)); if (!out) return NULL; }
            out[n++] = (uint16_t)cp;
        }
    }
    out[n] = 0;
    if (out_len) *out_len = n;
    return out;
}

char *vb_utf16_to_utf8(const uint16_t *s, size_t len)
{
    size_t i, n = 0;
    char *out;
    if (!s) return NULL;
    out = (char *)malloc(len * 3 + 4);
    if (!out) return NULL;
    for (i = 0; i < len; i++) {
        uint32_t cp = s[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < len &&
            s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            i++;
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
            cp = 0xFFFD;
        }
        if (cp < 0x80) {
            out[n++] = (char)cp;
        } else if (cp < 0x800) {
            out[n++] = (char)(0xC0 | (cp >> 6));
            out[n++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out[n++] = (char)(0xE0 | (cp >> 12));
            out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[n++] = (char)(0x80 | (cp & 0x3F));
        } else {
            out[n++] = (char)(0xF0 | (cp >> 18));
            out[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[n++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    out[n] = 0;
    return out;
}

/* ---------------------------------------------------------------- wav file */

static void put32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void put16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

uint32_t vb_wav_header(uint8_t *o, uint32_t pcm_len, uint16_t channels,
                       uint32_t rate, uint16_t bits)
{
    uint16_t block = (uint16_t)(channels * (bits / 8));
    uint32_t byte_rate = rate * block;
    memcpy(o + 0, "RIFF", 4);
    put32le(o + 4, 36 + pcm_len);
    memcpy(o + 8, "WAVE", 4);
    memcpy(o + 12, "fmt ", 4);
    put32le(o + 16, 16);
    put16le(o + 20, 1);              /* PCM */
    put16le(o + 22, channels);
    put32le(o + 24, rate);
    put32le(o + 28, byte_rate);
    put16le(o + 32, block);
    put16le(o + 34, bits);
    memcpy(o + 36, "data", 4);
    put32le(o + 40, pcm_len);
    return 44 + pcm_len;
}

/* -------------------------------------------------------------------- json */

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

static const char *find_key(const char *json, const char *key)
{
    size_t klen = strlen(key);
    const char *p = json;
    while ((p = strchr(p, '"')) != NULL) {
        if (strncmp(p + 1, key, klen) == 0 && p[1 + klen] == '"') {
            const char *q = skip_ws(p + 2 + klen);
            if (*q == ':') return skip_ws(q + 1);
        }
        p++;
    }
    return NULL;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static unsigned hex4(const char *p)
{
    unsigned v = 0;
    int i;
    for (i = 0; i < 4; i++) {
        int h = hexval((unsigned char)p[i]);
        if (h < 0) return 0xFFFD;
        v = (v << 4) | (unsigned)h;
    }
    return v;
}

static void put_utf8(char *out, size_t *n, uint32_t cp)
{
    char tmp[4];
    size_t k = 0, i;
    if (cp < 0x80) {
        tmp[k++] = (char)cp;
    } else if (cp < 0x800) {
        tmp[k++] = (char)(0xC0 | (cp >> 6));
        tmp[k++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        tmp[k++] = (char)(0xE0 | (cp >> 12));
        tmp[k++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[k++] = (char)(0x80 | (cp & 0x3F));
    } else {
        tmp[k++] = (char)(0xF0 | (cp >> 18));
        tmp[k++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        tmp[k++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[k++] = (char)(0x80 | (cp & 0x3F));
    }
    for (i = 0; i < k; i++) out[(*n)++] = tmp[i];
}

static char *parse_json_string(const char *p)
{
    size_t cap, n = 0;
    char *out;
    if (*p != '"') return NULL;
    p++;
    cap = strlen(p) + 8;
    out = (char *)malloc(cap);
    if (!out) return NULL;
    while (*p && *p != '"') {
        if (*p == '\\') {
            uint32_t cp;
            p++;
            switch (*p) {
            case 'n': cp = '\n'; p++; break;
            case 't': cp = '\t'; p++; break;
            case 'r': cp = '\r'; p++; break;
            case 'b': cp = '\b'; p++; break;
            case 'f': cp = '\f'; p++; break;
            case '/': cp = '/';  p++; break;
            case '\\': cp = '\\'; p++; break;
            case '"': cp = '"';  p++; break;
            case 'u': {
                unsigned v = hex4(p + 1);
                p += 5;
                if (v >= 0xD800 && v <= 0xDBFF && p[0] == '\\' && p[1] == 'u') {
                    unsigned lo = hex4(p + 2);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        v = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
                        p += 6;
                    }
                }
                cp = v;
                break;
            }
            default:
                if (!*p) { out[n] = 0; return out; }
                cp = (unsigned char)*p;
                p++;
                break;
            }
            put_utf8(out, &n, cp);
        } else {
            out[n++] = *p++;
        }
    }
    out[n] = 0;
    return out;
}

char *vb_json_get_string(const char *json, const char *key)
{
    const char *v;
    if (!json || !key) return NULL;
    v = find_key(json, key);
    if (!v) return NULL;
    return parse_json_string(v);
}

char *vb_json_get_first_string(const char *json, const char *const *keys, int nkeys)
{
    int i;
    for (i = 0; i < nkeys; i++) {
        char *s = vb_json_get_string(json, keys[i]);
        if (s) return s;
    }
    return NULL;
}

/* --------------------------------------------------------------- multipart */

static size_t text_len(const char *s) { return s ? strlen(s) : 0; }

static void put_mem(char **p, const void *src, size_t n)
{
    if (n) memcpy(*p, src, n);
    *p += n;
}

uint8_t *vb_multipart_body(const char *boundary, const VbMultipart *mp, uint32_t *out_len)
{
    size_t total = 0;
    int i;
    char *buf, *p;
    uint8_t *out;

    for (i = 0; i < mp->n; i++) {
        total += 2 + strlen(boundary) + 2;                     /* --boundary\r\n */
        total += 40 + text_len(mp->names[i]) + 4;              /* Content-Disposition ... */
        total += 2;                                            /* \r\n */
        total += text_len(mp->values[i]) + 2;                  /* value + \r\n */
    }
    total += 2 + strlen(boundary) + 2;
    total += 60 + text_len(mp->file_field) + text_len(mp->file_name);
    total += 30 + text_len(mp->file_type);
    total += 2;
    total += mp->file_len + 2;
    total += 2 + strlen(boundary) + 4 + 2;                     /* --boundary--\r\n */
    total += 512;                                              /* slack */

    buf = (char *)malloc(total);
    if (!buf) return NULL;
    p = buf;

    for (i = 0; i < mp->n; i++) {
        const char *name = mp->names[i];
        const char *val = mp->values[i];
        put_mem(&p, "--", 2);
        put_mem(&p, boundary, strlen(boundary));
        put_mem(&p, "\r\n", 2);
        put_mem(&p, "Content-Disposition: form-data; name=\"", 38);
        put_mem(&p, name, strlen(name));
        put_mem(&p, "\"\r\n\r\n", 5);
        if (val) put_mem(&p, val, strlen(val));
        put_mem(&p, "\r\n", 2);
    }

    put_mem(&p, "--", 2);
    put_mem(&p, boundary, strlen(boundary));
    put_mem(&p, "\r\n", 2);
    put_mem(&p, "Content-Disposition: form-data; name=\"", 38);
    put_mem(&p, mp->file_field, strlen(mp->file_field));
    put_mem(&p, "\"; filename=\"", 13);
    put_mem(&p, mp->file_name, strlen(mp->file_name));
    put_mem(&p, "\"\r\n", 3);
    put_mem(&p, "Content-Type: ", 14);
    put_mem(&p, mp->file_type, strlen(mp->file_type));
    put_mem(&p, "\r\n\r\n", 4);
    if (mp->file_len) put_mem(&p, mp->file_data, mp->file_len);
    put_mem(&p, "\r\n--", 4);
    put_mem(&p, boundary, strlen(boundary));
    put_mem(&p, "--\r\n", 4);

    if (out_len) *out_len = (uint32_t)(p - buf);
    out = (uint8_t *)buf;
    return out;
}
