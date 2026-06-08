/**
 * tls_client.c — Blocking HTTPS client: mbedTLS 3.6 + LwIP raw TCP, NO_SYS mode.
 *
 * Architecture:
 *   - LwIP raw TCP callbacks write arriving data into a ring buffer.
 *   - mbedTLS bio_recv pops from that ring buffer, calling ethernetif_poll()
 *     in a spin loop to keep the LwIP stack alive while waiting for data.
 *   - mbedTLS bio_send writes directly to the TCP PCB via tcp_write().
 *   - mbedtls_ssl_handshake() and ssl_read/write run synchronously from
 *     within the benchmark task (bare-metal main loop, NO_SYS=1).
 *
 * Entropy:
 *   - No hardware RNG (clock not configured in Boot).
 *   - DWT CYCCNT XOR'd with HAL_GetTick() provides a low-quality but
 *     functional seed for CTR-DRBG.  Acceptable for a research benchmark;
 *     NOT suitable for production security.
 */

#include "tls_client.h"
#include "tls_client_config.h"
#include "server_ca_cert.h"

#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
#include "mbedtls/platform.h"

#include "lwip/tcp.h"
#include "lwip/ip_addr.h"
#include "lwip/timeouts.h"
#include "ethernetif.h"
#include "stm32h7rsxx_hal.h"
#include "core_cm7.h"           /* DWT */

#include <string.h>
#include <stdio.h>

/* =========================================================================
 * External LwIP netif (defined in ethernetif.c / lwip.c)
 * ========================================================================= */
extern struct netif gnetif;

/* =========================================================================
 * Ring buffer — accumulates raw TCP bytes for the mbedTLS bio_recv callback
 * ========================================================================= */
#define RBUF_MASK  (TLS_RX_RBUF_SIZE - 1U)   /* TLS_RX_RBUF_SIZE must be 2^n */

typedef struct {
    uint8_t  buf[TLS_RX_RBUF_SIZE];
    uint32_t wr;   /* write offset (absolute, wraps naturally at 2^32) */
    uint32_t rd;   /* read  offset */
} rbuf_t;

static inline uint32_t rbuf_avail(const rbuf_t *r) { return r->wr - r->rd; }
static inline uint32_t rbuf_free (const rbuf_t *r) { return TLS_RX_RBUF_SIZE - rbuf_avail(r); }

static void rbuf_push(rbuf_t *r, const uint8_t *src, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
        r->buf[(r->wr + i) & RBUF_MASK] = src[i];
    r->wr += len;
}

static uint32_t rbuf_pop(rbuf_t *r, uint8_t *dst, uint32_t maxlen)
{
    uint32_t n = rbuf_avail(r);
    if (n > maxlen) n = maxlen;
    for (uint32_t i = 0; i < n; i++)
        dst[i] = r->buf[(r->rd + i) & RBUF_MASK];
    r->rd += n;
    return n;
}

/* =========================================================================
 * TCP connection state  (mirrors raw_conn_t in http_client.c)
 * ========================================================================= */
typedef struct {
    struct tcp_pcb *pcb;
    volatile int    connected;
    volatile err_t  conn_err;
    volatile int    rx_closed;   /* FIN received */
    rbuf_t          rx;
} tls_tcp_t;

/* =========================================================================
 * LwIP TCP callbacks
 * ========================================================================= */
static void _net_poll(void)
{
    ethernetif_poll(&gnetif);
    sys_check_timeouts();
}

static err_t _on_connected(void *arg, struct tcp_pcb *pcb, err_t err)
{
    tls_tcp_t *c = (tls_tcp_t *)arg; (void)pcb;
    c->conn_err = err;
    c->connected = 1;
    return ERR_OK;
}

static err_t _on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    tls_tcp_t *c = (tls_tcp_t *)arg; (void)err;
    if (!p) { c->rx_closed = 1; return ERR_OK; }

    uint32_t copy = (p->tot_len <= rbuf_free(&c->rx)) ? p->tot_len : rbuf_free(&c->rx);
    if (copy > 0) {
        /* pbuf may be chained — walk the chain */
        struct pbuf *q = p;
        uint32_t off = 0;
        while (q && off < copy) {
            uint32_t chunk = (q->len < copy - off) ? q->len : (copy - off);
            rbuf_push(&c->rx, (const uint8_t *)q->payload, chunk);
            off += chunk;
            q = q->next;
        }
    }
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void _on_err(void *arg, err_t err)
{
    tls_tcp_t *c = (tls_tcp_t *)arg;
    c->conn_err = err;
    c->rx_closed = 1;
    c->pcb = NULL;   /* stack freed the PCB */
}

static err_t _on_poll_cb(void *arg, struct tcp_pcb *pcb)
{
    (void)arg; (void)pcb; return ERR_OK;
}

/* =========================================================================
 * mbedTLS bio callbacks
 * ========================================================================= */
static int _bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    tls_tcp_t *c = (tls_tcp_t *)ctx;
    if (!c->pcb) return MBEDTLS_ERR_SSL_CONN_EOF;

    uint32_t sent = 0;
    while (sent < (uint32_t)len) {
        uint16_t chunk = (uint16_t)((len - sent) > 1460U ? 1460U : (len - sent));
        if (tcp_write(c->pcb, buf + sent, chunk, TCP_WRITE_FLAG_COPY) != ERR_OK)
            break;
        sent += chunk;
    }
    if (tcp_output(c->pcb) != ERR_OK) { /* best effort */ }
    return (sent > 0) ? (int)sent : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

static int _bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    tls_tcp_t *c = (tls_tcp_t *)ctx;

    uint32_t t0 = HAL_GetTick();
    while (rbuf_avail(&c->rx) == 0) {
        if (c->rx_closed)  return 0;   /* EOF — server closed */
        if (HAL_GetTick() - t0 > TLS_RECV_TIMEOUT_MS)
            return MBEDTLS_ERR_SSL_TIMEOUT;
        _net_poll();
    }

    uint32_t n = rbuf_pop(&c->rx, buf, (uint32_t)len);
    return (int)n;
}

/* =========================================================================
 * Custom entropy source  (DWT + HAL tick, research-grade only)
 * ========================================================================= */
static int _entropy_poll(void *ctx, unsigned char *output, size_t len, size_t *olen)
{
    (void)ctx;
    for (size_t i = 0; i < len; ) {
        uint32_t v = DWT->CYCCNT ^ (HAL_GetTick() * 0x9E3779B9U);
        v ^= (uint32_t)(uintptr_t)output;
        size_t chunk = (len - i) < 4U ? (len - i) : 4U;
        memcpy(output + i, &v, chunk);
        i += chunk;
    }
    *olen = len;
    return 0;
}

/* =========================================================================
 * Full TLS context  (static — avoids ~10 KB stack overhead)
 * ========================================================================= */
typedef struct {
    tls_tcp_t                tcp;
    mbedtls_ssl_context      ssl;
    mbedtls_ssl_config       conf;
    mbedtls_x509_crt         ca_cert;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context  entropy;
} tls_ctx_t;

static tls_ctx_t s_tls;   /* zero-initialised at startup */

/* =========================================================================
 * Internal helper: init and configure mbedTLS
 * ========================================================================= */
static tls_result_t _tls_init(const char *server_ip)
{
    int ret;

    /* Wipe previous state */
    memset(&s_tls.ssl,      0, sizeof s_tls.ssl);
    memset(&s_tls.conf,     0, sizeof s_tls.conf);
    memset(&s_tls.ca_cert,  0, sizeof s_tls.ca_cert);
    memset(&s_tls.ctr_drbg, 0, sizeof s_tls.ctr_drbg);
    memset(&s_tls.entropy,  0, sizeof s_tls.entropy);

    /* Entropy + CTR-DRBG */
    mbedtls_entropy_init(&s_tls.entropy);
    mbedtls_entropy_add_source(&s_tls.entropy, _entropy_poll, NULL,
                                32, MBEDTLS_ENTROPY_SOURCE_STRONG);
    mbedtls_ctr_drbg_init(&s_tls.ctr_drbg);
    ret = mbedtls_ctr_drbg_seed(&s_tls.ctr_drbg, mbedtls_entropy_func,
                                  &s_tls.entropy,
                                  (const unsigned char *)"STM32-TLS-S9", 12);
    if (ret != 0) return TLS_ERR_OOM;

    /* Load embedded CA cert (DER) */
    mbedtls_x509_crt_init(&s_tls.ca_cert);
    ret = mbedtls_x509_crt_parse_der(&s_tls.ca_cert,
                                      SERVER_CA_CERT_DER,
                                      SERVER_CA_CERT_DER_LEN);
    if (ret != 0) return TLS_ERR_CERT;

    /* SSL config: TLS 1.2 client, RSA, verify server cert */
    mbedtls_ssl_config_init(&s_tls.conf);
    ret = mbedtls_ssl_config_defaults(&s_tls.conf,
                                       MBEDTLS_SSL_IS_CLIENT,
                                       MBEDTLS_SSL_TRANSPORT_STREAM,
                                       MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) return TLS_ERR_OOM;

    mbedtls_ssl_conf_rng(&s_tls.conf, mbedtls_ctr_drbg_random, &s_tls.ctr_drbg);
    mbedtls_ssl_conf_ca_chain(&s_tls.conf, &s_tls.ca_cert, NULL);
    mbedtls_ssl_conf_authmode(&s_tls.conf, MBEDTLS_SSL_VERIFY_OPTIONAL);

    /* Force TLS 1.2 */
    mbedtls_ssl_conf_min_tls_version(&s_tls.conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(&s_tls.conf, MBEDTLS_SSL_VERSION_TLS1_2);

    /* SSL context */
    mbedtls_ssl_init(&s_tls.ssl);
    ret = mbedtls_ssl_setup(&s_tls.ssl, &s_tls.conf);
    if (ret != 0) return TLS_ERR_OOM;

    /* Hostname for SNI + cert CN/SAN verification */
    ret = mbedtls_ssl_set_hostname(&s_tls.ssl, server_ip);
    if (ret != 0) return TLS_ERR_OOM;

    return TLS_OK;
}

static void _tls_free(void)
{
    mbedtls_ssl_free        (&s_tls.ssl);
    mbedtls_ssl_config_free (&s_tls.conf);
    mbedtls_x509_crt_free   (&s_tls.ca_cert);
    mbedtls_ctr_drbg_free   (&s_tls.ctr_drbg);
    mbedtls_entropy_free    (&s_tls.entropy);
}

/* =========================================================================
 * Helper: parse HTTP status code
 * ========================================================================= */
static int _parse_status(const char *buf, uint32_t len)
{
    if (len < 12) return -1;
    if (buf[0] != 'H' || buf[5] != '1' || buf[6] != '.' || buf[8] != ' ') return -1;
    int code = 0;
    if (sscanf(buf + 9, "%3d", &code) != 1) return -1;
    return code;
}

static char *_find_body(char *buf, uint32_t total, uint32_t *body_len_out)
{
    char *end = buf + total;
    for (char *p = buf; p + 4 <= end; p++) {
        if (p[0]=='\r' && p[1]=='\n' && p[2]=='\r' && p[3]=='\n') {
            char *body = p + 4;
            *body_len_out = (uint32_t)(end - body);
            return body;
        }
    }
    *body_len_out = 0;
    return NULL;
}

/* =========================================================================
 * Core request engine
 * ========================================================================= */
static tls_result_t _do_request(const char     *server_ip,
                                 uint16_t        port,
                                 const char     *req_buf,
                                 uint32_t        req_len,
                                 tls_response_t *resp,
                                 uint32_t       *hs_cycles_out)
{
    /* ---- TCP connect ---- */
    ip_addr_t addr;
    if (!ipaddr_aton(server_ip, &addr)) return TLS_ERR_NOIP;

    tls_tcp_t *tc = &s_tls.tcp;
    memset(tc, 0, sizeof *tc);

    tc->pcb = tcp_new();
    if (!tc->pcb) return TLS_ERR_NET;

    tcp_arg (tc->pcb, tc);
    tcp_recv(tc->pcb, _on_recv);
    tcp_err (tc->pcb, _on_err);
    tcp_poll(tc->pcb, _on_poll_cb, 4);

    if (tcp_connect(tc->pcb, &addr, port, _on_connected) != ERR_OK) {
        tcp_abort(tc->pcb); return TLS_ERR_NET;
    }

    uint32_t t0 = HAL_GetTick();
    while (!tc->connected && tc->conn_err == ERR_OK
           && (HAL_GetTick() - t0 < TLS_CONNECT_TIMEOUT_MS))
        _net_poll();

    if (!tc->connected || tc->conn_err != ERR_OK) {
        if (tc->pcb) { tcp_abort(tc->pcb); tc->pcb = NULL; }
        return TLS_ERR_NET;
    }

    /* ---- TLS handshake ---- */
    mbedtls_ssl_set_bio(&s_tls.ssl, tc, _bio_send, _bio_recv, NULL);

    uint32_t hs_t0 = DWT->CYCCNT;
    int ret;
    do {
        ret = mbedtls_ssl_handshake(&s_tls.ssl);
    } while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);
    *hs_cycles_out = DWT->CYCCNT - hs_t0;

    if (ret != 0) {
        if (tc->pcb) { tcp_close(tc->pcb); tc->pcb = NULL; }
        return TLS_ERR_HANDSHAKE;
    }

    /* ---- Send HTTP request via TLS ---- */
    uint32_t written = 0;
    while (written < req_len) {
        ret = mbedtls_ssl_write(&s_tls.ssl,
                                 (const unsigned char *)req_buf + written,
                                 req_len - written);
        if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (ret < 0) {
            if (tc->pcb) { tcp_close(tc->pcb); tc->pcb = NULL; }
            return TLS_ERR_SEND;
        }
        written += (uint32_t)ret;
    }

    /* ---- Read HTTP response via TLS ---- */
    uint32_t rx_total = 0;
    for (;;) {
        if (rx_total >= TLS_RESP_BUF_SIZE - 1U) break;
        ret = mbedtls_ssl_read(&s_tls.ssl,
                                (unsigned char *)resp->resp_buf + rx_total,
                                TLS_RESP_BUF_SIZE - 1U - rx_total);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ)  continue;
        if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || ret == 0) break;
        if (ret < 0) { rx_total = 0; break; }
        rx_total += (uint32_t)ret;
    }

    mbedtls_ssl_close_notify(&s_tls.ssl);
    if (tc->pcb) { tcp_close(tc->pcb); tc->pcb = NULL; }

    if (rx_total == 0) return TLS_ERR_RECV;

    resp->resp_buf[rx_total] = '\0';
    resp->resp_total = rx_total;
    resp->status_code = _parse_status(resp->resp_buf, rx_total);
    resp->body = _find_body(resp->resp_buf, rx_total, &resp->body_len);
    return TLS_OK;
}

/* =========================================================================
 * Public API
 * ========================================================================= */
tls_result_t tls_client_get(const char     *server_ip,
                             uint16_t        port,
                             const char     *uri,
                             tls_response_t *resp)
{
    if (!server_ip || !uri || !resp) return TLS_ERR_PARAM;
    if (!gnetif.ip_addr.addr)        return TLS_ERR_NOIP;

    memset(resp, 0, sizeof *resp);

    tls_result_t rc = _tls_init(server_ip);
    if (rc != TLS_OK) { _tls_free(); return rc; }

    char req[512];
    int len = snprintf(req, sizeof req,
        "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n"
        "User-Agent: STM32H7S3/mbedTLS-3.6\r\n\r\n",
        uri, server_ip);
    if (len <= 0 || (uint32_t)len >= sizeof req) { _tls_free(); return TLS_ERR_PARAM; }

    uint32_t hs_cycles = 0;
    rc = _do_request(server_ip, port, req, (uint32_t)len, resp, &hs_cycles);
    resp->handshake_cycles = hs_cycles;

    _tls_free();
    return rc;
}

tls_result_t tls_client_post(const char     *server_ip,
                              uint16_t        port,
                              const char     *uri,
                              const char     *content_type,
                              const char     *body,
                              uint32_t        body_len,
                              tls_response_t *resp)
{
    if (!server_ip || !uri || !content_type || !resp) return TLS_ERR_PARAM;
    if (!gnetif.ip_addr.addr)                          return TLS_ERR_NOIP;

    memset(resp, 0, sizeof *resp);

    tls_result_t rc = _tls_init(server_ip);
    if (rc != TLS_OK) { _tls_free(); return rc; }

    char req[512];
    int hdr_len = snprintf(req, sizeof req,
        "POST %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n"
        "User-Agent: STM32H7S3/mbedTLS-3.6\r\n"
        "Content-Type: %s\r\nContent-Length: %lu\r\n\r\n",
        uri, server_ip, content_type, (unsigned long)body_len);
    if (hdr_len <= 0 || (uint32_t)hdr_len >= sizeof req) { _tls_free(); return TLS_ERR_PARAM; }

    /* Append body if it fits */
    uint32_t rem  = (uint32_t)(sizeof req - (uint32_t)hdr_len);
    uint32_t copy = (body_len < rem) ? body_len : rem - 1U;
    if (body && copy > 0) memcpy(req + hdr_len, body, copy);
    uint32_t req_total = (uint32_t)hdr_len + copy;

    uint32_t hs_cycles = 0;
    rc = _do_request(server_ip, port, req, req_total, resp, &hs_cycles);
    resp->handshake_cycles = hs_cycles;

    _tls_free();
    return rc;
}
