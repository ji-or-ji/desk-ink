#include "link_json.h"

#include <stdlib.h>
#include <string.h>

static const char *skip_ws(const char *p)
{
    if (p == NULL) {
        return NULL;
    }
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    return p;
}

static const char *skip_string(const char *p)
{
    p++;  /* 跳过起始引号 */
    while (*p) {
        if (*p == '\\' && p[1]) {
            p += 2;
            continue;
        }
        if (*p == '"') {
            return p + 1;
        }
        p++;
    }
    return p;
}

static const char *skip_value(const char *p)
{
    p = skip_ws(p);
    if (p == NULL) {
        return NULL;
    }
    if (*p == '"') {
        return skip_string(p);
    }
    if (*p == '{' || *p == '[') {
        char open = *p;
        char close = (open == '{') ? '}' : ']';
        int depth = 0;
        while (*p) {
            if (*p == '"') {
                p = skip_string(p);
                continue;
            }
            if (*p == open) {
                depth++;
            } else if (*p == close) {
                depth--;
                p++;
                if (depth == 0) {
                    return p;
                }
                continue;
            }
            p++;
        }
        return p;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']') {
        p++;
    }
    return p;
}

const char *json_obj_get(const char *obj, const char *key)
{
    if (obj == NULL || key == NULL) {
        return NULL;
    }
    const char *p = skip_ws(obj);
    if (p == NULL || *p != '{') {
        return NULL;
    }
    p++;
    size_t want = strlen(key);

    for (;;) {
        p = skip_ws(p);
        if (p == NULL || *p == '}' || *p != '"') {
            return NULL;
        }
        const char *kstart = p + 1;
        const char *kend = skip_string(p);
        size_t klen = (size_t)(kend - kstart - 1);
        p = skip_ws(kend);
        if (p == NULL || *p != ':') {
            return NULL;
        }
        p = skip_ws(p + 1);
        if (p == NULL) {
            return NULL;
        }
        if (klen == want && strncmp(kstart, key, want) == 0) {
            return p;
        }
        p = skip_value(p);
        p = skip_ws(p);
        if (p == NULL) {
            return NULL;
        }
        if (*p == ',') {
            p++;
            continue;
        }
        return NULL;
    }
}

static int hex4(const char *p, unsigned *out)
{
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') {
            v |= (unsigned)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            v |= (unsigned)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            v |= (unsigned)(c - 'A' + 10);
        } else {
            return -1;
        }
    }
    *out = v;
    return 0;
}

static size_t utf8_encode(unsigned cp, char *out, size_t cap)
{
    if (cp < 0x80) {
        if (cap < 1) return 0;
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        if (cap < 2) return 0;
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        if (cap < 3) return 0;
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cap < 4) return 0;
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

bool json_value_str(const char *value, char *out, size_t out_size)
{
    if (value == NULL || out == NULL || out_size == 0) {
        return false;
    }
    value = skip_ws(value);
    if (value == NULL || *value != '"') {
        return false;
    }
    value++;
    size_t o = 0;
    while (*value && *value != '"') {
        char c = *value++;
        if (c == '\\') {
            char e = *value++;
            switch (e) {
            case '"':  c = '"';  break;
            case '\\': c = '\\'; break;
            case '/':  c = '/';  break;
            case 'n':  c = '\n'; break;
            case 't':  c = '\t'; break;
            case 'r':  c = '\r'; break;
            case 'b':  c = '\b'; break;
            case 'f':  c = '\f'; break;
            case 'u': {
                unsigned cp = 0;
                if (hex4(value, &cp) != 0) {
                    continue;
                }
                value += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF &&
                    value[0] == '\\' && value[1] == 'u') {
                    unsigned lo = 0;
                    if (hex4(value + 2, &lo) == 0) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        value += 6;
                    }
                }
                o += utf8_encode(cp, out + o, out_size - 1 - o);
                continue;
            }
            default:
                c = e;
                break;
            }
        }
        if (o + 1 < out_size) {
            out[o++] = c;
        }
    }
    out[o] = '\0';
    return true;
}

bool json_value_int(const char *value, int *out)
{
    if (value == NULL || out == NULL) {
        return false;
    }
    value = skip_ws(value);
    if (value == NULL) {
        return false;
    }
    char *end = NULL;
    long v = strtol(value, &end, 10);
    if (end == value) {
        return false;
    }
    *out = (int)v;
    return true;
}

bool json_value_bool(const char *value, bool *out)
{
    if (value == NULL || out == NULL) {
        return false;
    }
    value = skip_ws(value);
    if (value == NULL) {
        return false;
    }
    if (strncmp(value, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(value, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

const char *json_array_first(const char *arr)
{
    if (arr == NULL) {
        return NULL;
    }
    const char *p = skip_ws(arr);
    if (p == NULL || *p != '[') {
        return NULL;
    }
    p = skip_ws(p + 1);
    if (p == NULL || *p == ']' || *p == '\0') {
        return NULL;
    }
    return p;
}

const char *json_array_next(const char *cur)
{
    if (cur == NULL) {
        return NULL;
    }
    const char *p = skip_value(cur);
    p = skip_ws(p);
    if (p == NULL || *p != ',') {
        return NULL;
    }
    p = skip_ws(p + 1);
    if (p == NULL || *p == ']' || *p == '\0') {
        return NULL;
    }
    return p;
}
