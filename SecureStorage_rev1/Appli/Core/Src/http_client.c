/**
 * http_client.c — Blocking HTTP GET/POST using LwIP raw TCP API.
 *
 * Hardware: NUCLEO-H7S3L8 (STM32H7S3L8Hx)
 * Mode:     NO_SYS=1 bare-metal polling (no FreeRTOS, no netconn)
 *
 * Each request spins in a polling loop calling ethernetif_poll() and
 * sys_check_timeouts() until the TCP connection completes, the response
 * is received, and the server closes the connection.
 */

#include "http_client.h"
#include "http_client_config.h"
#include "ethernetif.h"         /* ethernetif_poll() */

#include <stdio.h>
#include <string.h>

#include "lwip/tcp.h"
#include "lwip/ip_addr.h"
#include "lwip/timeouts.h"
#include "lwip/pbuf.h"
#include "stm32h7rsxx_hal.h"    /* HAL_GetTick() */

/* -------------------------------------------------------------------------
 * Net polling helper — call from tight spin loops to keep LwIP alive.
 * -------------------------------------------------------------------------*/
extern struct netif gnetif;

static void _net_poll(void)
{
    ethernetif_poll(&gnetif);
    sys_check_timeouts();
}

/* -------------------------------------------------------------------------
 * Raw TCP connection state
 * -------------------------------------------------------------------------*/
typedef struct {
    struct tcp_pcb *pcb;        /* NULL after error (TCP stack already freed it) */
    volatile int    connected;  /* 1 when SYN-ACK received */
    volatile err_t  conn_err;   /* error from connected or err callback */
    volatile int    rx_done;    /* 1 when FIN received (server closed) */
    char           *rx_buf;     /* destination buffer (resp->resp_buf) */
    uint32_t        rx_cap;     /* capacity of rx_buf (HTTP_RESP_BUF_SIZE) */
    uint32_t        rx_len;     /* bytes received so far */
} raw_conn_t;

/* -------------------------------------------------------------------------
 * TCP callbacks (called from within _net_poll → sys_check_timeouts or
 * ethernetif_poll → netif->input → tcp_input in the NO_SYS context).
 * -------------------------------------------------------------------------*/

static err_t _on_connected(void *arg, struct tcp_pcb *pcb, err_t err)
{
    raw_conn_t *c = (raw_conn_t *)arg;
    (void)pcb;
    c->conn_err = err;
    c->connected = 1;
    return ERR_OK;
}

static err_t _on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    raw_conn_t *c = (raw_conn_t *)arg;
    (void)err;

    if (p == NULL)
    {
        /* FIN received — server closed the connection */
        c->rx_done = 1;
        return ERR_OK;
    }

    /* Copy as much as fits into the response buffer */
    uint32_t avail = (c->rx_cap > c->rx_len + 1U) ? (c->rx_cap - c->rx_len - 1U) : 0U;
    uint32_t copy  = (p->tot_len < avail) ? p->tot_len : avail;
    if (copy > 0U)
    {
        pbuf_copy_partial(p, c->rx_buf + c->rx_len, (u16_t)copy, 0);
        c->rx_len += copy;
    }

    tcp_recved(pcb, p->tot_len);  /* acknowledge to the remote side */
    pbuf_free(p);
    return ERR_OK;
}

static void _on_err(void *arg, err_t err)
{
    raw_conn_t *c = (raw_conn_t *)arg;
    c->conn_err = err;
    c->rx_done  = 1;
    c->pcb      = NULL;  /* TCP stack has already freed the PCB */
}

static err_t _on_poll(void *arg, struct tcp_pcb *pcb)
{
    (void)arg;
    (void)pcb;
    return ERR_OK;
}

/* -------------------------------------------------------------------------
 * Internal helpers (identical to the original netconn version)
 * -------------------------------------------------------------------------*/

static int _parse_status_code(const char *buf, uint32_t len)
{
    if (len < 12) return -1;
    if (buf[0] != 'H' || buf[5] != '1' || buf[6] != '.' || buf[8] != ' ')
        return -1;
    int code = 0;
    if (sscanf(buf + 9, "%3d", &code) != 1) return -1;
    return code;
}

static char *_find_body(char *buf, uint32_t total, uint32_t *body_len_out)
{
    char *pos = buf;
    char *end = buf + total;
    while (pos + 4 <= end) {
        if (pos[0]=='\r' && pos[1]=='\n' && pos[2]=='\r' && pos[3]=='\n') {
            char *body = pos + 4;
            *body_len_out = (uint32_t)(end - body);
            return body;
        }
        pos++;
    }
    *body_len_out = 0;
    return NULL;
}

/* -------------------------------------------------------------------------
 * Core request/response engine — raw TCP + polling spin loop
 * -------------------------------------------------------------------------*/

static http_client_result_t _do_request(const char      *server_ip,
                                         uint16_t         port,
                                         const char      *req_buf,
                                         uint32_t         req_len,
                                         http_response_t *resp)
{
    ip_addr_t addr;
    if (!ipaddr_aton(server_ip, &addr))
        return HTTP_CLIENT_ERR_NOIP;

    /* Initialise connection state */
    raw_conn_t conn;
    memset(&conn, 0, sizeof(conn));
    conn.rx_buf = resp->resp_buf;
    conn.rx_cap = HTTP_RESP_BUF_SIZE;

    /* Allocate a new TCP PCB */
    conn.pcb = tcp_new();
    if (!conn.pcb)
        return HTTP_CLIENT_ERR_NET;

    /* Bind callbacks */
    tcp_arg (conn.pcb, &conn);
    tcp_recv(conn.pcb, _on_recv);
    tcp_err (conn.pcb, _on_err);
    tcp_poll(conn.pcb, _on_poll, 4);  /* 4 × 500 ms = 2 s TCP poll interval */

    /* Initiate TCP connection (sends SYN) */
    err_t rc = tcp_connect(conn.pcb, &addr, port, _on_connected);
    if (rc != ERR_OK)
    {
        tcp_abort(conn.pcb);
        return HTTP_CLIENT_ERR_NET;
    }

    /* ---- Spin until connected ---- */
    uint32_t t0 = HAL_GetTick();
    while (!conn.connected && conn.conn_err == ERR_OK
           && (HAL_GetTick() - t0 < (uint32_t)HTTP_RECV_TIMEOUT_MS))
    {
        _net_poll();
    }

    if (!conn.connected || conn.conn_err != ERR_OK)
    {
        if (conn.pcb) { tcp_abort(conn.pcb); conn.pcb = NULL; }
        return HTTP_CLIENT_ERR_NET;
    }

    /* ---- Send HTTP request ---- */
    rc = tcp_write(conn.pcb, req_buf, (u16_t)req_len, TCP_WRITE_FLAG_COPY);
    if (rc != ERR_OK)
    {
        tcp_abort(conn.pcb); conn.pcb = NULL;
        return HTTP_CLIENT_ERR_SEND;
    }
    rc = tcp_output(conn.pcb);
    if (rc != ERR_OK)
    {
        tcp_abort(conn.pcb); conn.pcb = NULL;
        return HTTP_CLIENT_ERR_SEND;
    }

    /* ---- Spin until server closes (FIN) or timeout ---- */
    t0 = HAL_GetTick();
    while (!conn.rx_done
           && (HAL_GetTick() - t0 < (uint32_t)HTTP_RECV_TIMEOUT_MS))
    {
        _net_poll();
    }

    /* Close the PCB if still alive */
    if (conn.pcb)
    {
        tcp_close(conn.pcb);
        conn.pcb = NULL;
    }

    if (conn.rx_len == 0)
        return HTTP_CLIENT_ERR_RECV;

    /* NUL-terminate and parse */
    resp->resp_buf[conn.rx_len] = '\0';
    resp->resp_total = conn.rx_len;

    int code = _parse_status_code(resp->resp_buf, resp->resp_total);
    if (code < 0)
        return HTTP_CLIENT_ERR_PARSE;
    resp->status_code = code;
    resp->body = _find_body(resp->resp_buf, resp->resp_total, &resp->body_len);

    return HTTP_CLIENT_OK;
}

/* -------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/

http_client_result_t http_client_get(const char      *server_ip,
                                     uint16_t         port,
                                     const char      *uri,
                                     http_response_t *resp)
{
    if (!server_ip || !uri || !resp) return HTTP_CLIENT_ERR_PARAM;

    memset(resp, 0, sizeof(*resp));

    char req[512];
    int req_len = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "User-Agent: STM32H7S3/1.0\r\n"
        "\r\n",
        uri, server_ip);

    if (req_len <= 0 || (uint32_t)req_len >= sizeof(req))
        return HTTP_CLIENT_ERR_PARAM;

    return _do_request(server_ip, port, req, (uint32_t)req_len, resp);
}

http_client_result_t http_client_post(const char      *server_ip,
                                      uint16_t         port,
                                      const char      *uri,
                                      const char      *content_type,
                                      const char      *body,
                                      uint32_t         body_len,
                                      http_response_t *resp)
{
    if (!server_ip || !uri || !content_type || !resp) return HTTP_CLIENT_ERR_PARAM;
    if (body_len > 0 && !body) return HTTP_CLIENT_ERR_PARAM;

    memset(resp, 0, sizeof(*resp));

    char req[512];
    int hdr_len = snprintf(req, sizeof(req),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "User-Agent: STM32H7S3/1.0\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lu\r\n"
        "\r\n",
        uri, server_ip, content_type, (unsigned long)body_len);

    if (hdr_len <= 0 || (uint32_t)hdr_len >= sizeof(req))
        return HTTP_CLIENT_ERR_PARAM;

    uint32_t remaining = (uint32_t)(sizeof(req) - (uint32_t)hdr_len);
    uint32_t copy_len  = (body_len < remaining) ? body_len : remaining - 1U;
    if (body_len > 0)
        memcpy(req + hdr_len, body, copy_len);
    uint32_t req_total = (uint32_t)hdr_len + copy_len;

    return _do_request(server_ip, port, req, req_total, resp);
}
