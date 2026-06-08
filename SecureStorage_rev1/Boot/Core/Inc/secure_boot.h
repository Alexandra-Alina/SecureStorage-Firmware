#ifndef SECURE_BOOT_H
#define SECURE_BOOT_H

#define SECURE_BOOT_OK    0
#define SECURE_BOOT_FAIL  1

/**
 * Verify the Appli firmware in NOR flash using HMAC-SHA256.
 *
 * Reads APPLI_SIZE bytes from APPLI_BASE_ADDR (NOR mapped by ExtMem),
 * computes HMAC-SHA256 with the embedded HMAC_KEY, and compares the
 * result against EXPECTED_TAG using a constant-time comparison.
 *
 * Must be called after MX_EXTMEM_MANAGER_Init() maps the NOR flash.
 *
 * Returns SECURE_BOOT_OK (0) on success, SECURE_BOOT_FAIL (1) on mismatch.
 */
int secure_boot_verify(void);

#endif /* SECURE_BOOT_H */
