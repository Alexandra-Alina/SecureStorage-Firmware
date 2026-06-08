#ifndef TLS_CLIENT_CONFIG_H
#define TLS_CLIENT_CONFIG_H

/** IP and port of the HTTPS bench server (Python server_https.py). */
#define HTTPS_SERVER_IP    "136.113.205.153"
#define HTTPS_SERVER_PORT  8443U

/** Receive ring-buffer size (bytes).  Must be >= max TLS record = 4101 B. */
#define TLS_RX_RBUF_SIZE   8192U

/** Timeout waiting for TCP connection (ms). */
#define TLS_CONNECT_TIMEOUT_MS   5000U

/** Timeout waiting for TLS data / handshake packet (ms). */
#define TLS_RECV_TIMEOUT_MS     10000U

/** Max bytes accumulated from a single HTTPS response. */
#define TLS_RESP_BUF_SIZE   2048U

#endif /* TLS_CLIENT_CONFIG_H */
