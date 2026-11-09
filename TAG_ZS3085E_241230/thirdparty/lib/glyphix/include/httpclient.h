/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#ifndef HTTPCLIENT_H_
#define HTTPCLIENT_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef web_malloc
#define web_malloc malloc
#endif

#ifndef web_calloc
#define web_calloc calloc
#endif

#ifndef web_realloc
#define web_realloc realloc
#endif

#ifndef web_free
#define web_free free
#endif

#ifndef web_strdup
#define web_strdup strdup
#endif

#define HTTPCLIENT_HEADER_BUFSZ   4096
#define HTTPCLIENT_RESPONSE_BUFSZ 4096

enum HTTPCLIENT_STATUS {
    HTTPCLIENT_OK,
    HTTPCLIENT_ERROR,
    HTTPCLIENT_TIMEOUT,
    HTTPCLIENT_NOMEM,
    HTTPCLIENT_NOSOCKET,
    HTTPCLIENT_NOBUFFER,
    HTTPCLIENT_CONNECT_FAILED,
    HTTPCLIENT_DISCONNECT,
    HTTPCLIENT_FILE_ERROR,
};

enum HTTPCLIENT_METHOD {
    HTTPCLIENT_USER_METHOD,
    HTTPCLIENT_GET,
    HTTPCLIENT_POST,
};

struct httpclient_header {
    char *buffer;
    size_t length; /* content header buffer size */

    size_t size; /* maximum support header size */
};

struct httpclient_session {
    struct httpclient_header *header; /* httpclient response header information */
    int socket;
    int resp_status;

    char *host;    /* server host */
    char *req_url; /* HTTP request address*/

    int chunk_sz;
    int chunk_offset;

    int content_length;
    size_t content_remainder; /* remainder of content length */

    void *user_data;
};

typedef httpclient_session *httpclient_session_t;

struct httpclient_ops {
    /* create http session and set header response size */
    httpclient_session *(*session_create)(size_t header_sz);
    /* close and release httpclient session */
    int (*session_close)(httpclient_session *session);

    int (*set_timeout)(httpclient_session *session, int millisecond);

    /* send or receive data from server */
    int (*read)(httpclient_session *session, void *buffer, size_t size);
    int (*write)(httpclient_session *session, const void *buffer, size_t size);

    /* internal */
    int (*connect)(httpclient_session *session, const char *URI);
    int (*send_header)(httpclient_session *session, int method);
    int (*handle_response)(httpclient_session *session);
    int (*get_bytes_avail)(httpclient_session *session);
};

/* create httpclient session and set header response size */
httpclient_session *httpclient_session_create(size_t header_sz);
/* close and release httpclient session */
int httpclient_session_close(httpclient_session *session);

int httpclient_set_timeout(httpclient_session *session, int millisecond);

/* httpclient GET/POST header buffer operate by the header fields */
int httpclient_header_fields_add(struct httpclient_session *session, const char *fmt, ...);
const char *httpclient_header_fields_get(struct httpclient_session *session, const char *fields);
void httpclient_header_fields_iter(struct httpclient_session *session,
                                   bool (*iter)(void *user_handle, const char *key,
                                                const char *value),
                                   void *user_handle);

/* send or receive data from server */
int httpclient_read(httpclient_session *session, void *buffer, size_t size);
int httpclient_write(httpclient_session *session, const void *buffer, size_t size);

/* send HTTP POST/GET request, and get response data */
int httpclient_get_position(httpclient_session *session, const char *URI, int position);

/* internal */
int httpclient_connect(httpclient_session *session, const char *URI);
int httpclient_send_header(httpclient_session *session, int method);
int httpclient_handle_response(httpclient_session *session);
int httpclient_get_bytes_avail(httpclient_session *session);

void httpclient_set_ops(const struct httpclient_ops *ops);

#ifdef __cplusplus
}
#endif

#endif