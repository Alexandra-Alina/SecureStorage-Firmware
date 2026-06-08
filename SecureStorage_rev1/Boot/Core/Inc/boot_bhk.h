#ifndef BOOT_BHK_H
#define BOOT_BHK_H

/* Boot Hardware Key (BHK) — 32-byte AES-256 wrap key stored in Boot
 * internal flash. Protected by RDP Level 1 — unreadable via JTAG/SWD.
 * Loaded into SAES hardware registers during Boot; Appli uses SAES
 * without ever seeing this key in plaintext.
 * Different from HMAC_KEY — separate key, separate purpose. */
static const uint8_t BHK_WRAP_KEY[32] __attribute__((aligned(4))) = {
    0xB3, 0x7F, 0x21, 0xC4, 0x88, 0x5A, 0xE9, 0x16,
    0x4D, 0xF2, 0x0B, 0x73, 0xA6, 0x3E, 0x91, 0xC5,
    0x57, 0x0C, 0xDA, 0x84, 0x2F, 0xB1, 0x69, 0xE3,
    0x7A, 0x45, 0xFC, 0x28, 0x93, 0x1D, 0x60, 0xAE,
};

#endif /* BOOT_BHK_H */
