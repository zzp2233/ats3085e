#ifndef P4_JSON_H
#define P4_JSON_H
#include <stdint.h>
#include <stddef.h>
/* Bounded, allocation-free views. The complete document must validate first. */
struct p4_json { const char *p, *end; };
int p4_json_document(const char *data, size_t size, struct p4_json *out);
int p4_json_get(struct p4_json object, const char *key, struct p4_json *out);
int p4_json_string(struct p4_json value, char *out, size_t capacity);
int p4_json_u32(struct p4_json value, uint32_t *out);
int p4_json_array(struct p4_json value, struct p4_json *first);
int p4_json_at(struct p4_json value, unsigned int index, struct p4_json *out);
int p4_json_headers(struct p4_json value, char *out, size_t capacity);
#endif
