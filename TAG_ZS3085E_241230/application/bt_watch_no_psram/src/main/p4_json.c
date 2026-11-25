#include "p4_json.h"
#include <string.h>

static void space(const char **p, const char *end)
{
    while (*p < end && (**p == ' ' || **p == '\r' || **p == '\n' || **p == '\t')) ++*p;
}
static int hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
/* Decoding only ASCII is intentional for protocol identifiers, URLs and headers.
 * Unknown JSON values may contain UTF-8; their syntax is still fully consumed. */
static int string(const char **p, const char *end, char *out, size_t cap)
{
    size_t used = 0;
    if (*p == end || *(*p)++ != '"') return -1;
    while (*p < end) {
        unsigned int c = (unsigned char)*(*p)++;
        if (c == '"') {
            if (out) { if (used >= cap) return -1; out[used] = 0; }
            return (int)used;
        }
        if (c < 32) return -1;
        if (c == '\\') {
            if (*p == end) return -1;
            c = (unsigned char)*(*p)++;
            if (c == 'u') {
                unsigned int i;
                c = 0;
                for (i = 0; i < 4; ++i) {
                    int digit;
                    if (*p == end || (digit = hex(*(*p)++)) < 0) return -1;
                    c = c * 16u + (unsigned int)digit;
                }
                if (c >= 0xd800 && c <= 0xdbff) {
                    unsigned int low = 0;
                    if (end - *p < 6 || (*p)[0] != '\\' || (*p)[1] != 'u') return -1;
                    *p += 2;
                    for (i = 0; i < 4; ++i) {
                        int digit = hex(*(*p)++);
                        if (digit < 0) return -1;
                        low = low * 16u + (unsigned int)digit;
                    }
                    if (low < 0xdc00 || low > 0xdfff) return -1;
                    c = 0x10000;
                } else if (c >= 0xdc00 && c <= 0xdfff) return -1;
            } else if (c == 'b') c = 8;
            else if (c == 'f') c = 12;
            else if (c == 'n') c = 10;
            else if (c == 'r') c = 13;
            else if (c == 't') c = 9;
            else if (c != '"' && c != '\\' && c != '/') return -1;
        }
        if (out) {
            if (!c || c > 127 || used + 1 >= cap) return -1;
            out[used] = (char)c;
        }
        ++used;
    }
    return -1;
}
static int skip(const char **p, const char *end, unsigned int depth)
{
    char c;
    if (depth > 12) return -1;
    space(p, end);
    if (*p == end) return -1;
    c = **p;
    if (c == '"') return string(p, end, NULL, 0) < 0 ? -1 : 0;
    if (c == '{' || c == '[') {
        char close = c == '{' ? '}' : ']';
        ++*p;
        space(p, end);
        if (*p < end && **p == close) { ++*p; return 0; }
        for (;;) {
            if (c == '{') {
                if (string(p, end, NULL, 0) < 0) return -1;
                space(p, end);
                if (*p == end || *(*p)++ != ':') return -1;
            }
            if (skip(p, end, depth + 1)) return -1;
            space(p, end);
            if (*p == end) return -1;
            if (**p == close) { ++*p; return 0; }
            if (*(*p)++ != ',') return -1;
            space(p, end);
        }
    }
    if (c == 't' || c == 'f' || c == 'n') {
        const char *word = c == 't' ? "true" : c == 'f' ? "false" : "null";
        size_t n = strlen(word);
        if ((size_t)(end - *p) < n || memcmp(*p, word, n)) return -1;
        *p += n;
        return 0;
    }
    if (c == '-') ++*p;
    if (*p == end) return -1;
    if (**p == '0') ++*p;
    else {
        if (**p < '1' || **p > '9') return -1;
        do { ++*p; } while (*p < end && **p >= '0' && **p <= '9');
    }
    if (*p < end && **p == '.') {
        ++*p;
        if (*p == end || **p < '0' || **p > '9') return -1;
        do { ++*p; } while (*p < end && **p >= '0' && **p <= '9');
    }
    if (*p < end && (**p == 'e' || **p == 'E')) {
        ++*p;
        if (*p < end && (**p == '+' || **p == '-')) ++*p;
        if (*p == end || **p < '0' || **p > '9') return -1;
        do { ++*p; } while (*p < end && **p >= '0' && **p <= '9');
    }
    return 0;
}
int p4_json_document(const char *data, size_t size, struct p4_json *out)
{
    const char *p = data, *end = data + size;
    space(&p, end);
    out->p = p;
    if (skip(&p, end, 0)) return -1;
    out->end = p;
    space(&p, end);
    return p == end ? 0 : -1;
}
int p4_json_get(struct p4_json object, const char *key, struct p4_json *out)
{
    const char *p = object.p, *end = object.end;
    int found = 0;
    char name[80];
    if (p == end || *p++ != '{') return -1;
    space(&p, end);
    while (p < end && *p != '}') {
        const char *value;
        if (string(&p, end, name, sizeof(name)) < 0) return -1;
        space(&p, end);
        if (p == end || *p++ != ':') return -1;
        space(&p, end);
        value = p;
        if (skip(&p, end, 0)) return -1;
        if (!strcmp(name, key)) {
            if (found) return -1; /* Duplicate required fields are ambiguous. */
            out->p = value; out->end = p; found = 1;
        }
        space(&p, end);
        if (p < end && *p == ',') { ++p; space(&p, end); }
        else break;
    }
    return found;
}
int p4_json_string(struct p4_json value, char *out, size_t capacity)
{
    int n = string(&value.p, value.end, out, capacity);
    return n < 0 || value.p != value.end ? -1 : n;
}
int p4_json_u32(struct p4_json value, uint32_t *out)
{
    uint32_t n = 0;
    const char *p = value.p;
    if (p == value.end) return -1;
    while (p < value.end) {
        unsigned int d = (unsigned char)*p++ - '0';
        if (d > 9 || n > (UINT32_MAX - d) / 10u) return -1;
        n = n * 10u + d;
    }
    *out = n;
    return 0;
}
int p4_json_array(struct p4_json value, struct p4_json *first)
{
    const char *p = value.p;
    int count = 0;
    if (p == value.end || *p++ != '[') return -1;
    space(&p, value.end);
    while (p < value.end && *p != ']') {
        const char *start = p;
        if (skip(&p, value.end, 0)) return -1;
        if (!count && first) { first->p = start; first->end = p; }
        ++count;
        space(&p, value.end);
        if (p < value.end && *p == ',') { ++p; space(&p, value.end); }
        else break;
    }
    return count;
}
int p4_json_at(struct p4_json value, unsigned int index, struct p4_json *out)
{
    const char *p = value.p;
    unsigned int i = 0;
    if (p == value.end || *p++ != '[') return -1;
    space(&p, value.end);
    while (p < value.end && *p != ']') {
        const char *start = p;
        if (skip(&p, value.end, 0)) return -1;
        if (i++ == index) { out->p = start; out->end = p; return 0; }
        space(&p, value.end);
        if (p == value.end || *p++ != ',') break;
        space(&p, value.end);
    }
    return -1;
}
static int header_name(const char *s)
{
    static const char *reserved[] = { "host", "content-length", "connection",
        "transfer-encoding", "expect" };
    char lower[80];
    size_t i, j, n = strlen(s);
    if (!n || n >= sizeof(lower)) return -1;
    for (i = 0; i < n; ++i) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-')) return -1;
        lower[i] = c >= 'A' && c <= 'Z' ? c + 32 : c;
    }
    lower[n] = 0;
    if (!strncmp(lower, "x-device-", 9)) return -1;
    for (j = 0; j < sizeof(reserved) / sizeof(reserved[0]); ++j)
        if (!strcmp(lower, reserved[j])) return -1;
    return 0;
}
int p4_json_headers(struct p4_json value, char *out, size_t capacity)
{
    const char *p = value.p;
    size_t used = 0;
    char name[80];
    if (p == value.end || *p++ != '{' || !capacity) return -1;
    space(&p, value.end);
    while (p < value.end && *p != '}') {
        int n = string(&p, value.end, name, sizeof(name));
        size_t i, start;
        if (n < 0 || header_name(name) || used + (size_t)n + 4 >= capacity) return -1;
        /* HTTP names are case insensitive: reject duplicate signed headers. */
        for (start = 0; start < used;) {
            for (i = 0; i < (size_t)n; ++i) {
                char a = name[i], b = out[start + i];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) break;
            }
            if (i == (size_t)n && out[start + i] == ':') return -1;
            while (start < used && out[start] != '\n') ++start;
            ++start;
        }
        space(&p, value.end);
        if (p == value.end || *p++ != ':') return -1;
        space(&p, value.end);
        memcpy(out + used, name, n); used += n;
        out[used++] = ':'; out[used++] = ' ';
        start = used;
        n = string(&p, value.end, out + used, capacity - used);
        if (n < 0 || used + (size_t)n + 3 > capacity) return -1;
        used += n;
        for (i = start; i < used; ++i) if ((unsigned char)out[i] < 32 || out[i] >= 127) return -1;
        out[used++] = '\r'; out[used++] = '\n';
        space(&p, value.end);
        if (p < value.end && *p == ',') { ++p; space(&p, value.end); }
        else break;
    }
    out[used] = 0;
    return (int)used;
}
