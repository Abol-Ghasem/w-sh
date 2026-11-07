/* core.h - platform independent helpers (unit tested with a native compiler). */
#ifndef VB_CORE_H
#define VB_CORE_H

#include <stdint.h>
#include <stddef.h>

/* UTF-8 -> UTF-16. Returns malloc'ed buffer (NOT nul terminated guarantee: it
   is nul terminated when in is valid), sets *out_len to the number of units. */
uint16_t *vb_utf8_to_utf16(const char *s, size_t *out_len);

/* UTF-16 -> UTF-8, allocated, nul terminated. */
char *vb_utf16_to_utf8(const uint16_t *s, size_t len);

/* Writes a 44 byte canonical RIFF/WAVE header for PCM data.
   Returns the total file size (44 + pcm_len). out must hold >= 44 bytes. */
uint32_t vb_wav_header(uint8_t *out, uint32_t pcm_len, uint16_t channels,
                       uint32_t rate, uint16_t bits);

/* Extracts a string value for "key" from a JSON document.
   Returns malloc'ed UTF-8 (unescaped) or NULL when not found. */
char *vb_json_get_string(const char *json, const char *key);

/* Same as above but scans several keys in order, first hit wins. */
char *vb_json_get_first_string(const char *json, const char *const *keys, int nkeys);

typedef struct {
    const char *file_field;   /* e.g. "file"   */
    const char *file_name;    /* e.g. "audio.wav" */
    const char *file_type;    /* e.g. "audio/wav" */
    const uint8_t *file_data;
    uint32_t file_len;
    const char *const *names;   /* text fields */
    const char *const *values;
    int n;
} VbMultipart;

/* Builds a multipart/form-data body. Returns malloc'ed buffer, sets out_len. */
uint8_t *vb_multipart_body(const char *boundary, const VbMultipart *mp, uint32_t *out_len);

#endif /* VB_CORE_H */
