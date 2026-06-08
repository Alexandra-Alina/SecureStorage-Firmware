/**
 * mbedtls_config.h — Minimal TLS 1.2 RSA client config for STM32H7S3L8.
 *
 * Profile:
 *   - TLS 1.2 only (no TLS 1.3)
 *   - Key exchange: RSA  (no ECDHE — avoids ECC code)
 *   - Cipher suite: TLS-RSA-WITH-AES-128-GCM-SHA256
 *   - Cert verification: REQUIRED against embedded CA cert (server_ca_cert.h)
 *   - Entropy: custom DWT+HAL_GetTick source (no /dev/urandom, no RNG hw)
 *   - Buffer size: 4 KB in/out  (reduced from 16 KB default)
 *   - No PSA crypto, no ECC, no DTLS, no TLS 1.3
 */

#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

/* ------------------------------------------------------------------ */
/* Platform                                                             */
/* ------------------------------------------------------------------ */
#define MBEDTLS_HAVE_ASM
#define MBEDTLS_PLATFORM_C
/* Use stdlib malloc/free (provided by nano.specs + _sbrk in sysmem.c) */

/* ------------------------------------------------------------------ */
/* Entropy — custom DWT-based source; no OS entropy                    */
/* ------------------------------------------------------------------ */
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_NO_PLATFORM_ENTROPY

/* ------------------------------------------------------------------ */
/* RNG                                                                  */
/* ------------------------------------------------------------------ */
#define MBEDTLS_CTR_DRBG_C

/* ------------------------------------------------------------------ */
/* Hash                                                                 */
/* ------------------------------------------------------------------ */
#define MBEDTLS_MD_C
#define MBEDTLS_SHA1_C      /* X.509 cert OID / RSA PKCS#1 v1.5     */
#define MBEDTLS_SHA256_C    /* TLS 1.2 PRF; cipher suite SHA-256     */
#define MBEDTLS_SHA512_C    /* May be pulled in by internal mbedTLS  */

/* ------------------------------------------------------------------ */
/* Symmetric cipher                                                     */
/* ------------------------------------------------------------------ */
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C           /* AES-128-GCM for the cipher suite  */
#define MBEDTLS_CIPHER_MODE_CBC /* keep CBC available as fallback     */
#define MBEDTLS_CIPHER_PADDING_PKCS7
#define MBEDTLS_CIPHER_C

/* ------------------------------------------------------------------ */
/* RSA — used for TLS key exchange and X.509 cert verification         */
/* ------------------------------------------------------------------ */
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21

/* ------------------------------------------------------------------ */
/* PK (public key abstraction)                                          */
/* ------------------------------------------------------------------ */
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C

/* ------------------------------------------------------------------ */
/* ASN.1 / OID / PEM / BASE64                                          */
/* ------------------------------------------------------------------ */
#define MBEDTLS_OID_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C

/* ------------------------------------------------------------------ */
/* X.509 certificate parsing and verification                           */
/* ------------------------------------------------------------------ */
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C

/* ------------------------------------------------------------------ */
/* SSL / TLS                                                            */
/* ------------------------------------------------------------------ */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C       /* client only                        */
#define MBEDTLS_SSL_PROTO_TLS1_2
/* TLS 1.3 intentionally NOT enabled */

#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED

/* Reduce I/O buffer sizes from 16 KB to 4 KB */
#define MBEDTLS_SSL_IN_CONTENT_LEN   4096
#define MBEDTLS_SSL_OUT_CONTENT_LEN  4096

/* ------------------------------------------------------------------ */
/* Misc                                                                 */
/* ------------------------------------------------------------------ */
#define MBEDTLS_ERROR_C
#define MBEDTLS_VERSION_C
#define MBEDTLS_DEPRECATED_REMOVED

/* ------------------------------------------------------------------ */
/* Explicitly disabled                                                  */
/* ------------------------------------------------------------------ */
/* No MBEDTLS_USE_PSA_CRYPTO                                           */
/* No MBEDTLS_PSA_CRYPTO_C                                             */
/* No MBEDTLS_ECP_C / MBEDTLS_ECDH_C / MBEDTLS_ECDSA_C               */
/* No MBEDTLS_DHM_C                                                    */
/* No MBEDTLS_SSL_PROTO_TLS1_3                                         */
/* No MBEDTLS_TIMING_C (no DTLS)                                       */
/* No MBEDTLS_DEBUG_C  (save code space)                               */

#endif /* MBEDTLS_CONFIG_H */
