#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stdint.h>
#include "http_client_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Return codes for http_client_get() and http_client_post().
 */
typedef enum {
    HTTP_CLIENT_OK          =  0,   /* Request completed; status_code is valid  */
    HTTP_CLIENT_ERR_PARAM   = -1,   /* NULL argument passed                     */
    HTTP_CLIENT_ERR_NET     = -2,   /* netconn_new() or netconn_connect() failed */
    HTTP_CLIENT_ERR_SEND    = -3,   /* netconn_write() failed                   */
    HTTP_CLIENT_ERR_RECV    = -4,   /* netconn_recv() timed out or errored      */
    HTTP_CLIENT_ERR_PARSE   = -5,   /* Could not parse HTTP status line         */
    HTTP_CLIENT_ERR_BUF     = -6,   /* Response exceeded HTTP_RESP_BUF_SIZE     */
    HTTP_CLIENT_ERR_NOIP    = -7,   /* server_ip string failed ipaddr_aton()    */
} http_client_result_t;

/**
 * Holds the result of a completed HTTP request.
 *
 * Declare one of these on the caller's task stack or as a static variable.
 * Do NOT heap-allocate — the struct is ~2060 bytes.
 *
 * Fields are valid only when the function returns HTTP_CLIENT_OK.
 * 'body' points into resp_buf (do not free it separately).
 */
typedef struct {
    int      status_code;                   /* e.g. 200, 404                    */
    char    *body;                          /* pointer into resp_buf after \r\n\r\n */
    uint32_t body_len;                      /* length of body in bytes          */
    char     resp_buf[HTTP_RESP_BUF_SIZE];  /* raw response: headers + body     */
    uint32_t resp_total;                    /* total bytes stored in resp_buf   */
} http_response_t;

/**
 * Perform a blocking HTTP GET request.
 *
 * Must be called from a FreeRTOS task context (uses LwIP blocking netconn API).
 * LwIP must have been initialised (MX_LWIP_Init called) before using this.
 *
 * @param server_ip   Dotted-decimal string, e.g. "192.168.1.100"
 * @param port        TCP port (usually HTTP_SERVER_PORT = 8080)
 * @param uri         Request-URI, e.g. "/" or "/api/status"
 * @param resp        Caller-supplied response struct (stack or static)
 * @return            HTTP_CLIENT_OK on success; negative error code otherwise
 */
http_client_result_t http_client_get(const char      *server_ip,
                                     uint16_t         port,
                                     const char      *uri,
                                     http_response_t *resp);

/**
 * Perform a blocking HTTP POST request.
 *
 * @param server_ip    Dotted-decimal string
 * @param port         TCP port
 * @param uri          Request-URI, e.g. "/api/echo"
 * @param content_type MIME type, e.g. "application/json"
 * @param body         Request body bytes (may be NULL if body_len == 0)
 * @param body_len     Length of body in bytes
 * @param resp         Caller-supplied response struct
 * @return             HTTP_CLIENT_OK on success; negative error code otherwise
 */
http_client_result_t http_client_post(const char      *server_ip,
                                      uint16_t         port,
                                      const char      *uri,
                                      const char      *content_type,
                                      const char      *body,
                                      uint32_t         body_len,
                                      http_response_t *resp);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_CLIENT_H */
