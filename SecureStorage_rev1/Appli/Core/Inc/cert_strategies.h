#ifndef CERT_STRATEGIES_H
#define CERT_STRATEGIES_H

/*
 * cert_strategies.h — Public interface for the 6 TLS certificate/key
 * memory-allocation strategy benchmark.
 *
 * Call cert_strategies_run_all() once from main() after peripherals are up.
 * Results are printed over UART (printf → HAL_UART_Transmit on COM1).
 *
 * Strategies:
 *   1. ROM/static   — const array in .rodata, cert lives in XIP NOR (0x70000000)
 *   2. Stack        — VLA/fixed array on DTCM stack (0x20000000)
 *   3. Heap         — malloc/free, heap lives in DTCM / AXI SRAM
 *   4. Secure buf   — memcpy to .noncacheable_buffer region (0x24071C00)
 *   5. Encrypted    — AES-ECB-256 wraps private key, CRYP decrypts on use
 *   6. HW key load  — key loaded to CRYP registers, RAM copy zeroed
 */

void cert_strategies_run_all(void);

#endif /* CERT_STRATEGIES_H */
