#ifndef HTTP_CLIENT_CONFIG_H
#define HTTP_CLIENT_CONFIG_H

/**
 * IP address of the bench HTTP server running on the host PC.
 * Change this to your laptop's LAN IP address before flashing.
 *
 * Find your IP with: ipconfig  (Windows)
 * Example: "192.168.1.50"
 */
#define HTTP_SERVER_IP       "136.113.205.153"

/**
 * TCP port the server listens on.
 * Must match the port passed to server/server.py (default 5000).
 */
#define HTTP_SERVER_PORT     5000U

/**
 * Maximum bytes to accumulate from a single HTTP response (headers + body).
 * Responses larger than this are silently truncated at this size.
 * Keep <= 2048 to stay within HttpClientTask's 4096-byte stack budget.
 */
#define HTTP_RESP_BUF_SIZE   2048U

/**
 * Receive timeout in milliseconds for a single netconn_recv call.
 */
#define HTTP_RECV_TIMEOUT_MS 5000U

#endif /* HTTP_CLIENT_CONFIG_H */
