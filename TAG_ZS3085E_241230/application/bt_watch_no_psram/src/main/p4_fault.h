#ifndef P4_FAULT_H
#define P4_FAULT_H
#include "p4_upload.h"
#include <string.h>

/* Validation-only, per-invocation injection. No global or persistent state.
 * BEFORE simulates a request that never reached the server. LOST discards a
 * clean response after the real transaction, simulating an ambiguous outcome
 * at the business IO boundary, not a physical network or SPI failure. */
enum p4_fault_mode {
    P4_FAULT_PUT_BEFORE = 1u, P4_FAULT_PUT_LOST = 2u,
    P4_FAULT_COMPLETE_BEFORE = 4u, P4_FAULT_COMPLETE_LOST = 8u,
    P4_FAULT_PUT_403 = 16u, P4_FAULT_INIT_503 = 32u
};
struct p4_fault { unsigned int mode, fired; };
static int p4_fault_mode(const char *name)
{
    if (!strcmp(name, "put-before")) return P4_FAULT_PUT_BEFORE;
    if (!strcmp(name, "put-lost")) return P4_FAULT_PUT_LOST;
    if (!strcmp(name, "complete-before")) return P4_FAULT_COMPLETE_BEFORE;
    if (!strcmp(name, "complete-lost")) return P4_FAULT_COMPLETE_LOST;
    if (!strcmp(name, "both-lost")) return P4_FAULT_PUT_LOST | P4_FAULT_COMPLETE_LOST;
    if (!strcmp(name, "put-403")) return P4_FAULT_PUT_403;
    if (!strcmp(name, "init-503")) return P4_FAULT_INIT_503;
#ifdef P4_RADIO_TEST
    if (!strcmp(name, "radio-before")) return 64;
    if (!strcmp(name, "radio-mid")) return 128;
#endif
    return -1;
}
static int p4_fault_apply(struct p4_fault *f, const char *spec, bool after,
                          int ret, char *response, unsigned int *received, unsigned int *http)
{
    unsigned int bit = 0;
    bool put = !strncmp(spec, "PUT\n", 4);
    bool complete = !strncmp(spec, "POST\n" P4_ORIGIN P4_COMPLETE_PATH "\n",
                              sizeof("POST\n" P4_ORIGIN P4_COMPLETE_PATH "\n") - 1);
    if (after) {
        /* Never replace a real failure, including successful SPI recovery. */
        if (ret || *http < 200 || *http >= 300) return ret;
        if (put) bit = P4_FAULT_PUT_LOST;
        else if (complete) bit = P4_FAULT_COMPLETE_LOST;
    } else {
        if (put) bit = (f->mode & P4_FAULT_PUT_403) ? P4_FAULT_PUT_403 : P4_FAULT_PUT_BEFORE;
        else if (complete) bit = P4_FAULT_COMPLETE_BEFORE;
        else if (!strncmp(spec, "POST\n" P4_ORIGIN P4_INIT_PATH "\n",
                          sizeof("POST\n" P4_ORIGIN P4_INIT_PATH "\n") - 1)) bit = P4_FAULT_INIT_503;
    }
    if (!(f->mode & bit) || (f->fired & bit)) return ret;
    f->fired |= bit;
    /* Hide all response bytes and metadata from the business engine. */
    memset(response, 0, *received);
    response[0] = 0; *received = 0;
    *http = bit == P4_FAULT_PUT_403 ? 403u : bit == P4_FAULT_INIT_503 ? 503u : 0u;
    return *http ? P4_ERR_HTTP : P4_ERR_NETWORK;
}
#endif
