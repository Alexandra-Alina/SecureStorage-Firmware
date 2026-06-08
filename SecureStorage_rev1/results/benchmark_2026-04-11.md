# Secure Storage Benchmark — Live Results

**Board:** NUCLEO-H7S3L8 (STM32H7S3L8Hx, Cortex-M7)  
**Clock:** 600 MHz (PLL1, configured by Boot)  
**Build:** GCC arm-none-eabi 13.3 / nano.specs / -O0 -g3  
**Date:** 2026-04-11  
**UART:** COM6 @ 115200 8N1

---

## Summary Table

| # | Strategy | Cert / Key Address | Memory Region | RAM for Cert | Key in Plaintext RAM | Core Operation | Cycles | Time (µs) |
|---|----------|--------------------|---------------|:------------:|:--------------------:|----------------|-------:|----------:|
| 1 | ROM static | `0x7000ACB4` | XIP NOR | **0 B** | YES (ROM) | 2× XIP read | 600 | 1.00 |
| 2 | Stack | `0x2000FCA8` | DTCM | 272 B | YES (use) | memcpy NOR→stack | 3 620 | 6.03 |
| 3 | Heap | `0x200004F8` | DTCM | 272 B | YES (use) | malloc | 1 868 | 3.11 |
| 3 | Heap | — | DTCM | 272 B | YES (use) | memcpy NOR→heap | 3 148 | 5.25 |
| 3 | Heap | — | DTCM | 272 B | YES (use) | secure zero | 3 966 | 6.61 |
| 4 | Secure buf | `0x24071C00` | NONCACHE SRAM | 272 B (zeroed) | NO | NONCACHE write sim | 4 374 | 7.29 |
| 4 | Secure buf | `0x2000FEA8` | DTCM | 272 B (zeroed) | NO | NONCACHE read sim | 866 | 1.44 |
| 5 | AES-ECB-256 enc | `0x7000ADC4` → `0x24071D20` | NOR → NONCACHE | key encrypted | YES (~19 µs window) | AES-256 ECB encrypt | 16 938 | 28.23 |
| 5 | AES-ECB-256 dec | `0x24071D20` → `0x2000FF94` | NONCACHE → DTCM | key decrypted | YES (~19 µs window) | AES-256 ECB decrypt | 11 446 | 19.08 |
| 6 | HW CRYP offload | (CRYP K0R..K7R) | HW registers | 0 B after wipe | YES (0.94 µs only) | RAM key wipe | 566 | 0.94 |
| 6 | HW CRYP offload | — | HW registers | 0 B | NO | AES-256 ECB enc | 3 176 | 5.29 |
| 6 | HW CRYP offload | — | HW registers | 0 B | NO | AES-256 ECB dec | 3 620 | 6.03 |

---

## Strategy Detail

### S1 — ROM Static
```
cert_ptr address : 0x7000ACB4  (XIP NOR)
cert size        : 272 bytes
First byte       : 0x30  (DER SEQUENCE — PASS)
Last byte        : 0x00
DWT (2 XIP reads): 600 cycles  (1.00 µs)

RAM used for cert     : 0 bytes
Private key in ROM    : YES
Security verdict      : LOW — key in plaintext NOR, readable by debugger
```

### S2 — Stack
```
cert_stack addr  : 0x2000FCA8  (DTCM RAM)
cert size        : 272 bytes
DWT memcpy       : 3 620 cycles  (6.03 µs)
DWT TLS-use sim  : 318 cycles   (0.53 µs)
Stack watermark  : 0 / 512 bytes consumed  (watermark zone not hit)

RAM used for cert     : 272 bytes (stack, freed on return)
Private key in RAM    : YES during handshake
Security verdict      : MEDIUM — plaintext in DTCM stack; cleared on return
```

### S3 — Heap
```
cert_heap addr   : 0x200004F8  (DTCM RAM)
cert size        : 272 bytes
DWT malloc       : 1 868 cycles  (3.11 µs)
DWT memcpy       : 3 148 cycles  (5.25 µs)
Heap peak        : 272 bytes
DWT secure zero  : 3 966 cycles  (6.61 µs)

RAM used for cert     : 272 bytes (freed after handshake)
Private key in RAM    : YES during handshake
Security verdict      : MEDIUM — same exposure as stack; adds fragmentation risk
```

### S4 — Secure Storage Read-back (NONCACHE SRAM simulation)
```
s_nor_cert_buf   : 0x24071C00  (NONCACHE SRAM — correct section placement)
DWT NOR write sim: 4 374 cycles  (7.29 µs)
cert_ram addr    : 0x2000FEA8  (DTCM RAM)
DWT NOR read sim : 866 cycles   (1.44 µs)
DER header check : PASS (0x30 0x82 0x00 0xF8)

RAM used for cert     : 272 bytes (zeroed after use)
Private key exposed   : NO — cert is public; key stays in Secure Storage
Security verdict      : HIGH for public cert; use S5/S6 for private key
Production TODO       : MPU non-cacheable + XSPI2 DMA + D-Cache flush
```

### S5 — AES-ECB-256 Key Wrap / Unwrap (CRYP hardware)
```
CRYP instance    : 0x48020800  (CRYP peripheral base)
Source key (ROM) : 0x7000ADC4  (XIP NOR)
Encrypted key    : 0x24071D20  (NONCACHE SRAM)

DWT AES-256 ECB encrypt : 16 938 cycles  (28.23 µs)
Plaintext key  : F3 9A 6B 4C 82 17 DE 05 8E A1 C7 33 9F 4B 6A F2 ...
Encrypted key  : 82 26 19 37 FE E5 53 15 C3 C2 D7 F5 9B D1 6D 6B ...

key_plaintext addr      : 0x2000FF94  (DTCM RAM)
DWT AES-256 ECB decrypt : 11 446 cycles  (19.08 µs)
Decrypted key  : F3 9A 6B 4C 82 17 DE 05 8E A1 C7 33 9F 4B 6A F2 ...
Round-trip match        : PASS

Plaintext key in RAM    : YES — ~19.1 µs window (decrypt + sign)
Encrypted key at rest   : YES (NONCACHE SRAM / NOR in production)
Security verdict        : HIGH — key protected at rest
Production TODO         : SAES with device root key; TRNG-generated wrap key
```

### S6 — CRYP Hardware Key Offload (RAM wipe after HW load)
```
ram_key addr     : 0x2000FF90  (DTCM RAM — before wipe)
DWT RAM key wipe : 566 cycles  (0.94 µs)
ram_key after    : 00 00 00 00 ...  (confirmed zeroed)

Plaintext   : 00 11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF
Ciphertext  : 71 91 8C 38 A3 AA 9D 79 05 5E 7D 71 05 68 A3 73
Recovered   : 00 11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF
Round-trip  : PASS

DWT AES-256 ECB enc : 3 176 cycles  (5.29 µs)
DWT AES-256 ECB dec : 3 620 cycles  (6.03 µs)

CRYP K0LR=0x00000000  K0RR=0x00000000  (key registers are write-only — correct)

Plaintext key in RAM    : YES — only 566 cycles / 0.94 µs (memcpy + HAL_CRYP_Init)
Key in HW registers     : YES — CRYP K0R..K7R
Key readable via AHB    : YES (non-secure CRYP; SAES needed for isolation)
Security verdict        : VERY HIGH with SAES/TrustZone; HIGH with plain CRYP
Production TODO         : PKA for ECDSA; SAES + secure world; OTFDEC for NOR
```

---

## Key Observations

1. **AES-256 ECB encrypt (28.23 µs) is ~1.5× slower than decrypt (19.08 µs)**  
   This is expected: the HAL calls `HAL_CRYP_Encrypt` which performs the full key schedule on every call (`CRYP_KEYIVCONFIG_ALWAYS`). A single-use `KeyIVConfigSkip` or pre-expanded key would narrow the gap.

2. **S6 minimises key exposure to 0.94 µs** — the window from `memcpy(ram_key, ROM_WRAP_KEY)` through `HAL_CRYP_Init()` until the volatile wipe. With SAES (secure world only), the key would never appear in the non-secure AHB address space at all.

3. **CRYP key registers read as zero** — the CRYP peripheral correctly implements write-only key storage; the HAL reads back `0x00000000` for all key register fields.

4. **NONCACHE buffer lands at exactly 0x24071C00** — the linker script `.noncacheable_buffer (NOLOAD)` section is correctly placed. Production code must add an MPU region marking this range as Device/Strongly-Ordered for DMA coherency.

5. **Heap (S3) lives in DTCM** (`0x200004F8`), not AXI SRAM — `_sbrk` uses `_end` from the `._user_heap_stack` section placed in DTCM by the linker script. DTCM is faster (0-wait-state) but limited to 64 KB.

6. **Stack watermark shows 0 / 512 consumed** in S2 — the 512-byte paint zone was allocated *after* the cert_stack, so it was not touched by the function frame. The cert_stack itself consumed ~272+ bytes of the DTCM stack.

---

## Implementation Files

| File | Role |
|------|------|
| `Appli/Core/Inc/cert_test_data.h` | 272-byte synthetic DER cert, 32-byte ECC key, 32-byte AES-256 wrap key |
| `Appli/Core/Inc/cert_strategies.h` | Public interface: `cert_strategies_run_all()` |
| `Appli/Core/Src/cert_strategies.c` | 6 strategy implementations with full printf diagnostics |
| `Appli/Core/Inc/mem_measure.h` | DWT counter, stack watermark, heap tracking API |
| `Appli/Core/Src/mem_measure.c` | Implementations |
| `Appli/Core/Src/main.c` | Entry point; calls `cert_strategies_run_all()` after BSP init |
