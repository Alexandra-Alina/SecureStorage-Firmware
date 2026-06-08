#ifndef TLS_CLIENT_H
#define TLS_CLIENT_H

#include <stdint.h>
#include "tls_client_config.h"

typedef enum {
    TLS_OK           =  0,
    TLS_ERR_NOIP     = -1,   /* no IP address assigned to netif     */
    TLS_ERR_NET      = -2,   /* TCP connect failed                  */
    TLS_ERR_OOM      = -3,   /* mbedTLS malloc failed               */
    TLS_ERR_HANDSHAKE= -4,   /* TLS handshake error                 */
    TLS_ERR_CERT     = -5,   /* certificate verification failed     */
    TLS_ERR_SEND     = -6,   /* TLS write error                     */
    TLS_ERR_RECV     = -7,   /* TLS read timeout/error              */
    TLS_ERR_PARAM    = -8,   /* invalid arguments                   */
} tls_result_t;

typedef struct {
    int      status_code;        /* HTTP status (200, 201, …)        */
    char    *body;               /* pointer into resp_buf            */
    uint32_t body_len;
    char     resp_buf[TLS_RESP_BUF_SIZE];
    uint32_t resp_total;
    uint32_t handshake_cycles;   /* DWT cycles for TLS handshake     */
} tls_response_t;

/**
 * Blocking HTTPS GET over LwIP raw TCP + mbedTLS (NO_SYS mode).
 * Connects, handshakes, sends GET, reads response, tears down.
 */
tls_result_t tls_client_get(const char      *server_ip,
                             uint16_t         port,
                             const char      *uri,
                             tls_response_t  *resp);

/**
 * Blocking HTTPS POST — same mechanics as GET.
 */
tls_result_t tls_client_post(const char      *server_ip,
                              uint16_t         port,
                              const char      *uri,
                              const char      *content_type,
                              const char      *body,
                              uint32_t         body_len,
                              tls_response_t  *resp);

#endif /* TLS_CLIENT_H */
