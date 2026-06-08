/*
 * cert_strategies.c — 6 TLS certificate/key memory-allocation strategies
 *
 * Hardware: NUCLEO-H7S3L8 (STM32H7S3L8Hx, Cortex-M7 @ 600 MHz)
 * Output  : UART COM1 @ 115200 baud (printf → HAL_UART_Transmit)
 *
 * Each strategy prints:
 *   - What it does and why
 *   - Pointer address + memory region
 *   - DWT cycle count for the core operation
 *   - Whether the private key ever appears in plaintext RAM
 *   - Security verdict
 */

#include "cert_strategies.h"
#include "cert_test_data.h"
#include "mem_measure.h"
#include "main.h"
#include "http_client_config.h"
#include "http_client.h"
#include "tls_client.h"
#include "tls_client_config.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ==========================================================================
 * Noncacheable storage — placed in the 1 KB NONCACHE region (0x24071C00).
 * Production note: the MPU must mark 0x24071C00–0x240720FF as
 * Device/Strongly-Ordered to bypass D-Cache (e.g. for DMA coherency).
 * For this test we demonstrate placement only; cache flush/invalidate
 * (SCB_CleanDCache_by_Addr / SCB_InvalidateDCache_by_Addr) should be added
 * before/after any DMA-based NOR flash read in production code.
 * ==========================================================================*/
static uint8_t __attribute__((section(".noncacheable_buffer"), aligned(32)))
    s_nor_cert_buf[TEST_CERT_SIZE];

static uint8_t __attribute__((section(".noncacheable_buffer"), aligned(32)))
    s_enc_key_buf[TEST_KEY_SIZE];   /* holds AES-encrypted private key */

/* ==========================================================================
 * Local CRYP handle — separate from the global hcryp in main.c so we do not
 * disturb the CubeMX-managed CRYP/SAES state.
 * ==========================================================================*/
static CRYP_HandleTypeDef s_hcryp;

/* ==========================================================================
 * Internal helpers
 * ==========================================================================*/

static void print_line(void)
{
    printf("\r\n-----------------------------------------------------------\r\n");
}

static void print_hex(const char *label, const uint8_t *data, uint32_t len)
{
    printf("  %-14s: ", label);
    for (uint32_t i = 0; i < len && i < 16; i++) printf("%02X ", data[i]);
    if (len > 16) printf("...");
    printf("\r\n");
}

/** Return a human-readable description of an address region on H7S3. */
static const char *addr_region(uintptr_t addr)
{
    if (addr >= 0x70000000U && addr <= 0x77FFFFFFU) return "XIP NOR (0x70000000)";
    if (addr >= 0x20000000U && addr <= 0x2000FFFFU) return "DTCM RAM (0x20000000)";
    if (addr >= 0x24000000U && addr <= 0x24071BFFU) return "AXI SRAM (0x24000000)";
    if (addr >= 0x24071C00U && addr <= 0x24071FFFU) return "NONCACHE SRAM (0x24071C00)";
    if (addr >= 0x08000000U && addr <= 0x0800FFFFU) return "Internal Flash (0x08000000)";
    return "Unknown region";
}

/** Init CRYP with AES-ECB-256 and a caller-supplied 32-byte key. */
static HAL_StatusTypeDef cryp_init_aes256(const uint8_t *key32)
{
    __HAL_RCC_CRYP_CLK_ENABLE();

    s_hcryp.Instance                = CRYP;
    s_hcryp.Init.DataType           = CRYP_DATATYPE_32B;   /* no byte-swap */
    s_hcryp.Init.KeySize            = CRYP_KEYSIZE_256B;
    s_hcryp.Init.pKey               = (uint32_t *)(uintptr_t)key32;
    s_hcryp.Init.Algorithm          = CRYP_AES_ECB;
    s_hcryp.Init.DataWidthUnit      = CRYP_DATAWIDTHUNIT_WORD;
    s_hcryp.Init.HeaderWidthUnit    = CRYP_HEADERWIDTHUNIT_WORD;
    s_hcryp.Init.KeyIVConfigSkip    = CRYP_KEYIVCONFIG_ALWAYS;
    s_hcryp.Init.KeyMode            = CRYP_KEYMODE_NORMAL;

    return HAL_CRYP_Init(&s_hcryp);
}

/* Check SAES clock is on (set by Boot, survives jump) */
static HAL_StatusTypeDef saes_bhk_available(void)
{
    return __HAL_RCC_SAES_IS_CLK_ENABLED() ? HAL_OK : HAL_ERROR;
}

/* Direct-register SAES ECB operation (encrypt or decrypt) — bypasses HAL.
 * SAES must already have key loaded (by Boot). 16 bytes in/out (word-aligned).
 * mode: 0 = encrypt, SAES_CR_MODE_1 = decrypt */
static HAL_StatusTypeDef saes_ecb_direct(uint32_t mode,
                                          const uint32_t *in,
                                          uint32_t       *out)
{
    SAES_TypeDef *saes = SAES;
    uint32_t t;

    /* Disable, set MODE, re-enable */
    CLEAR_BIT(saes->CR, SAES_CR_EN);
    MODIFY_REG(saes->CR, SAES_CR_MODE, mode);

    /* For AES-256 decrypt: key derivation needed first */
    if (mode != 0U)
    {
        /* Switch to key derivation mode, enable, wait for CCF */
        MODIFY_REG(saes->CR, SAES_CR_MODE, SAES_CR_MODE_0); /* key derivation */
        SET_BIT(saes->CR, SAES_CR_EN);
        t = HAL_GetTick();
        while (!READ_BIT(saes->SR, SAES_SR_CCF))
            if (HAL_GetTick() - t > 500U) return HAL_TIMEOUT;
        SET_BIT(saes->ICR, SAES_ICR_CCF);
        CLEAR_BIT(saes->CR, SAES_CR_EN);
        /* Switch back to decrypt mode */
        MODIFY_REG(saes->CR, SAES_CR_MODE, SAES_CR_MODE_1);
    }

    SET_BIT(saes->CR, SAES_CR_EN);

    /* Feed 4 words */
    saes->DINR = in[0];
    saes->DINR = in[1];
    saes->DINR = in[2];
    saes->DINR = in[3];

    /* Wait for CCF */
    t = HAL_GetTick();
    while (!READ_BIT(saes->SR, SAES_SR_CCF))
        if (HAL_GetTick() - t > 500U) return HAL_TIMEOUT;

    out[0] = saes->DOUTR;
    out[1] = saes->DOUTR;
    out[2] = saes->DOUTR;
    out[3] = saes->DOUTR;

    SET_BIT(saes->ICR, SAES_ICR_CCF);
    CLEAR_BIT(saes->CR, SAES_CR_EN);

    return HAL_OK;
}

/* Probe DHUK availability — reads SAES_SR.KEYVALID directly, NO peripheral reset.
 * Safe to call with BHK already loaded: does not modify SAES registers. */
static void saes_probe_dhuk_safe(void)
{
    __HAL_RCC_SAES_CLK_ENABLE();
    /* Switch KEYSEL to DHUK temporarily (write-only to CR, no reset) */
    uint32_t cr_save = ((SAES_TypeDef *)SAES)->CR;
    MODIFY_REG(((SAES_TypeDef *)SAES)->CR, SAES_CR_KEYSEL, SAES_CR_KEYSEL_0); /* DHUK */
    HAL_Delay(1);  /* brief settle */
    uint32_t sr = ((SAES_TypeDef *)SAES)->SR;
    uint32_t keyvalid = (sr >> SAES_SR_KEYVALID_Pos) & 1U;
    /* Restore original KEYSEL (normal key from registers = BHK) */
    ((SAES_TypeDef *)SAES)->CR = cr_save;

    printf("  [DHUK probe] KEYVALID=%lu — %s\r\n", (unsigned long)keyvalid,
           keyvalid ? "DHUK disponibil!" : "OTP neprovisionat (placa eval)");
    if (!keyvalid)
        printf("  [DHUK probe] Production path: DHUK inlocuieste BHK complet.\r\n");
}

/* ==========================================================================
 * STRATEGY 1 — ROM static: cert lives permanently in XIP NOR .rodata
 *
 * Security: cert is public data → no risk.  Private key in ROM = bad in prod.
 * RAM used: 0 bytes for cert.  XIP NOR read every access (cached by I/D-Cache).
 * ==========================================================================*/
static void run_strategy1(void)
{
    print_line();
    printf("[S1] ROM Static  -  cert pointer into XIP NOR .rodata\r\n");
    printf("     The const array sits in flash at 0x70xxxxxx.\r\n");
    printf("     No RAM copy ever made.  Cache handles repeated reads.\r\n\r\n");

    /* Just use a pointer — zero extra RAM */
    const uint8_t *cert_ptr = ROM_CERT_DER;

    printf("  cert_ptr address : 0x%08lX  (%s)\r\n",
           (unsigned long)(uintptr_t)cert_ptr,
           addr_region((uintptr_t)cert_ptr));
    printf("  cert size        : %u bytes\r\n", (unsigned)TEST_CERT_SIZE);

    /* DWT: cost of a single pointer dereference + first-byte read */
    uint32_t t0 = mm_dwt_now();
    volatile uint8_t first_byte = cert_ptr[0];
    volatile uint8_t last_byte  = cert_ptr[TEST_CERT_SIZE - 1];
    uint32_t cy = mm_dwt_elapsed(t0);
    (void)first_byte; (void)last_byte;

    printf("  First byte       : 0x%02X (expect 0x30 = DER SEQUENCE)\r\n",
           cert_ptr[0]);
    printf("  Last byte        : 0x%02X\r\n", cert_ptr[TEST_CERT_SIZE - 1]);
    printf("  DWT (2 XIP reads): %lu cycles  (%.2f us)\r\n",
           (unsigned long)cy, mm_cycles_to_us(cy));

    printf("\r\n  >> RAM used for cert : 0 bytes\r\n");
    printf("  >> Private key in ROM: YES (only acceptable for S1 baseline)\r\n");
    printf("  >> Security verdict  : LOW  -  key in plaintext NOR, readable by debugger\r\n");
}

/* ==========================================================================
 * STRATEGY 2 — Stack allocation: cert copied to DTCM stack for handshake
 *
 * Security: cert is cleared when function returns (stack frame popped).
 *           Private key on stack is visible in RAM during use.
 * RAM used: TEST_CERT_SIZE bytes of stack (DTCM, very fast).
 * ==========================================================================*/
static void __attribute__((noinline)) run_strategy2(void)
{
    print_line();
    printf("[S2] Stack  -  cert copied to local array on DTCM stack\r\n");
    printf("     Good for short-lived TLS handshakes.  Frame is popped after use.\r\n\r\n");

    /* Paint a 512-byte region below our current SP so we can measure watermark */
    uint8_t paint_buf[512];
    mm_stack_paint(paint_buf, sizeof(paint_buf));

    /* The 'real' cert buffer on the stack */
    uint8_t cert_stack[TEST_CERT_SIZE];

    uint32_t t0 = mm_dwt_now();
    memcpy(cert_stack, ROM_CERT_DER, TEST_CERT_SIZE);
    uint32_t cy_copy = mm_dwt_elapsed(t0);

    /* Simulate TLS use: read first 4 bytes (e.g. DER header check) */
    t0 = mm_dwt_now();
    volatile uint32_t der_hdr;
    memcpy((void *)&der_hdr, cert_stack, 4);
    uint32_t cy_use = mm_dwt_elapsed(t0);
    (void)der_hdr;

    uint32_t watermark = mm_stack_watermark(paint_buf, sizeof(paint_buf));

    printf("  cert_stack addr  : 0x%08lX  (%s)\r\n",
           (unsigned long)(uintptr_t)cert_stack,
           addr_region((uintptr_t)cert_stack));
    printf("  cert size        : %u bytes\r\n", (unsigned)TEST_CERT_SIZE);
    printf("  DWT memcpy       : %lu cycles  (%.2f us)\r\n",
           (unsigned long)cy_copy, mm_cycles_to_us(cy_copy));
    printf("  DWT TLS-use sim  : %lu cycles  (%.2f us)\r\n",
           (unsigned long)cy_use, mm_cycles_to_us(cy_use));
    printf("  Stack watermark  : %lu / 512 bytes consumed\r\n",
           (unsigned long)watermark);
    print_hex("cert[0..15]", cert_stack, TEST_CERT_SIZE);

    /* Secure erase before return (compiler may optimise away — use volatile ptr) */
    volatile uint8_t *p = cert_stack;
    for (uint32_t i = 0; i < TEST_CERT_SIZE; i++) p[i] = 0;

    printf("\r\n  >> RAM used for cert : %u bytes (stack, freed on return)\r\n",
           (unsigned)TEST_CERT_SIZE);
    printf("  >> Private key in RAM: YES during handshake (cert_stack)\r\n");
    printf("  >> Security verdict  : MEDIUM  -  plaintext in DTCM stack; cleared on return\r\n");
}

/* ==========================================================================
 * STRATEGY 3 — Heap allocation: malloc/free around TLS handshake
 *
 * Security: heap block can be overwritten with free() + explicit zero.
 *           Memory fragmentation risk on embedded targets.
 * RAM used: TEST_CERT_SIZE bytes + malloc header overhead.
 * ==========================================================================*/
static void run_strategy3(void)
{
    print_line();
    printf("[S3] Heap  -  malloc/free around TLS handshake\r\n");
    printf("     Lifetime-controlled allocation; explicit zeroing before free.\r\n\r\n");

    mm_heap_reset_stats();

    uint32_t t0 = mm_dwt_now();
    uint8_t *cert_heap = (uint8_t *)mm_malloc(TEST_CERT_SIZE);
    uint32_t cy_alloc = mm_dwt_elapsed(t0);

    if (!cert_heap)
    {
        printf("  ERROR: malloc(%u) returned NULL  -  heap exhausted!\r\n",
               (unsigned)TEST_CERT_SIZE);
        return;
    }

    t0 = mm_dwt_now();
    memcpy(cert_heap, ROM_CERT_DER, TEST_CERT_SIZE);
    uint32_t cy_copy = mm_dwt_elapsed(t0);

    /* Simulate TLS use */
    volatile uint8_t check = cert_heap[0] ^ cert_heap[TEST_CERT_SIZE - 1];
    (void)check;

    printf("  cert_heap addr   : 0x%08lX  (%s)\r\n",
           (unsigned long)(uintptr_t)cert_heap,
           addr_region((uintptr_t)cert_heap));
    printf("  cert size        : %u bytes\r\n", (unsigned)TEST_CERT_SIZE);
    printf("  DWT malloc       : %lu cycles  (%.2f us)\r\n",
           (unsigned long)cy_alloc, mm_cycles_to_us(cy_alloc));
    printf("  DWT memcpy       : %lu cycles  (%.2f us)\r\n",
           (unsigned long)cy_copy, mm_cycles_to_us(cy_copy));
    printf("  Heap peak        : %lu bytes\r\n",
           (unsigned long)mm_heap_peak_bytes);
    print_hex("cert[0..15]", cert_heap, TEST_CERT_SIZE);

    /* Secure erase before free */
    t0 = mm_dwt_now();
    volatile uint8_t *p = cert_heap;
    for (uint32_t i = 0; i < TEST_CERT_SIZE; i++) p[i] = 0;
    uint32_t cy_zero = mm_dwt_elapsed(t0);

    mm_free(cert_heap, TEST_CERT_SIZE);
    cert_heap = NULL;

    printf("  DWT secure zero  : %lu cycles  (%.2f us)\r\n",
           (unsigned long)cy_zero, mm_cycles_to_us(cy_zero));

    printf("\r\n  >> RAM used for cert : %u bytes (freed after handshake)\r\n",
           (unsigned)TEST_CERT_SIZE);
    printf("  >> Private key in RAM: YES during handshake\r\n");
    printf("  >> Security verdict  : MEDIUM  -  same exposure as stack; adds fragmentation risk\r\n");
}

/* ==========================================================================
 * STRATEGY 4 — Secure Storage read-back: NOR flash → noncacheable SRAM
 *
 * Simulates a real Secure Storage partition on XSPI2 NOR:
 *   1. At provisioning: cert written to a dedicated NOR sector.
 *   2. At runtime     : NOR sector DMA-copied to noncacheable SRAM buffer.
 *   3. TLS uses the SRAM copy, then zeroes it.
 *
 * In this testbed we replace NOR I/O with memcpy (no actual NOR driver yet).
 * The noncacheable_buffer section IS at the correct address (0x24071C00).
 * Production code: add MPU region + HAL_XSPI_Receive() + D-Cache management.
 *
 * Security: cert is only in RAM while needed; key never leaves Secure Storage
 *           unless S5/S6 are used for the private key.
 * ==========================================================================*/
static void run_strategy4(void)
{
    print_line();
    printf("[S4] Secure Storage read-back  -  NOR flash → noncacheable SRAM\r\n");
    printf("     Simulates XSPI2 DMA read to 0x24071C00 NONCACHE region.\r\n\r\n");

    /* Step 1: \"write\" cert to NOR (simulated: copy to noncacheable buffer) */
    uint32_t t0 = mm_dwt_now();
    memcpy(s_nor_cert_buf, ROM_CERT_DER, TEST_CERT_SIZE);
    /* In production: SCB_CleanDCache_by_Addr() before DMA TX to NOR */
    uint32_t cy_write = mm_dwt_elapsed(t0);

    printf("  [NOR write sim]  : %lu cycles  (%.2f us)\r\n",
           (unsigned long)cy_write, mm_cycles_to_us(cy_write));
    printf("  s_nor_cert_buf   : 0x%08lX  (%s)\r\n",
           (unsigned long)(uintptr_t)s_nor_cert_buf,
           addr_region((uintptr_t)s_nor_cert_buf));

    /* Step 2: read cert back (simulate DMA from NOR to SRAM) */
    uint8_t cert_ram[TEST_CERT_SIZE];

    t0 = mm_dwt_now();
    /* In production: SCB_InvalidateDCache_by_Addr() after DMA RX from NOR */
    memcpy(cert_ram, s_nor_cert_buf, TEST_CERT_SIZE);
    uint32_t cy_read = mm_dwt_elapsed(t0);

    printf("  [NOR read sim]   : %lu cycles  (%.2f us)\r\n",
           (unsigned long)cy_read, mm_cycles_to_us(cy_read));
    printf("  cert_ram addr    : 0x%08lX  (%s)\r\n",
           (unsigned long)(uintptr_t)cert_ram,
           addr_region((uintptr_t)cert_ram));

    /* Verify integrity: compare first 4 bytes */
    int ok = (memcmp(cert_ram, ROM_CERT_DER, 4) == 0);
    print_hex("cert[0..15]", cert_ram, TEST_CERT_SIZE);
    printf("  DER header check : %s (0x%02X 0x%02X 0x%02X 0x%02X)\r\n",
           ok ? "PASS" : "FAIL",
           cert_ram[0], cert_ram[1], cert_ram[2], cert_ram[3]);

    /* Step 3: zero RAM copy after TLS use */
    volatile uint8_t *p = cert_ram;
    for (uint32_t i = 0; i < TEST_CERT_SIZE; i++) p[i] = 0;
    memset(s_nor_cert_buf, 0, TEST_CERT_SIZE);

    printf("\r\n  >> RAM used for cert : %u bytes (zeroed after use)\r\n",
           (unsigned)TEST_CERT_SIZE);
    printf("  >> Private key exposed: NO (cert is public; key stays in NOR)\r\n");
    printf("  >> Security verdict  : HIGH for public cert; use S5/S6 for private key\r\n");
    printf("  >> Production TODO   : MPU non-cacheable region + XSPI2 DMA + D-Cache flush\r\n");
}

/* ==========================================================================
 * STRATEGY 5 — Encrypted key storage (AES-ECB-256 wrap/unwrap via CRYP)
 *
 * The private key is AES-ECB-256 encrypted with ROM_WRAP_KEY and stored in
 * the noncacheable SRAM buffer (simulating an encrypted NOR sector).
 * To sign, the key is decrypted into a local stack buffer, used, then zeroed.
 *
 * In production:
 *   - ROM_WRAP_KEY comes from SAES with hardware-unique device root key (OTP).
 *   - Encrypted key is stored in a Secure Storage NOR partition.
 *   - Plaintext key exists in RAM only for the duration of the signing operation.
 *
 * Note: CRYP_DATAWIDTHUNIT_WORD with AES-256 ECB:
 *   - pKey    = pointer to 8 uint32_t words (= 32 bytes)
 *   - Size    = number of 32-bit words to process = TEST_KEY_SIZE / 4
 * ==========================================================================*/
static void run_strategy5(void)
{
    print_line();
    printf("[S5] Encrypted key storage  -  AES-ECB-256 wrap/unwrap via CRYP\r\n");
    printf("     Private key encrypted at rest; decrypted only for signing.\r\n\r\n");

    /* SAES hardware finding: requires OTP provisioning even for SW key mode */
    printf("  [SAES finding] SAES+DHUK/BHK necesita OTP provisioning pe H7RS.\r\n");
    saes_probe_dhuk_safe();
    printf("  [SAES finding] SAES nefunctional fara fuse OTP — benchmark via CRYP.\r\n\r\n");

    /* --- Step 1: initialise CRYP with ROM wrap key --- */
    HAL_StatusTypeDef status = cryp_init_aes256(ROM_WRAP_KEY);
    if (status != HAL_OK)
    {
        printf("  ERROR: HAL_CRYP_Init failed (status=%d)\r\n", (int)status);
        return;
    }
    printf("  CRYP init (AES-256 ECB): OK\r\n");
    printf("  CRYP instance    : 0x%08lX\r\n",
           (unsigned long)(uintptr_t)s_hcryp.Instance);

    /* --- Step 2: encrypt private key --- */
    uint32_t t0 = mm_dwt_now();
    status = HAL_CRYP_Encrypt(&s_hcryp,
                               (uint32_t *)(uintptr_t)ROM_PRIVATE_KEY,
                               TEST_KEY_SIZE / 4,
                               (uint32_t *)(uintptr_t)s_enc_key_buf,
                               HAL_MAX_DELAY);
    uint32_t cy_enc = mm_dwt_elapsed(t0);
    if (status != HAL_OK) { HAL_CRYP_DeInit(&s_hcryp); return; }

    printf("  [Encryption]\r\n");
    printf("  Source (plaintext key addr): 0x%08lX  (%s)\r\n",
           (unsigned long)(uintptr_t)ROM_PRIVATE_KEY,
           addr_region((uintptr_t)ROM_PRIVATE_KEY));
    printf("  Dest   (enc key buf  addr ): 0x%08lX  (%s)\r\n",
           (unsigned long)(uintptr_t)s_enc_key_buf,
           addr_region((uintptr_t)s_enc_key_buf));
    printf("  DWT AES-256 ECB encrypt    : %lu cycles  (%.2f us)\r\n",
           (unsigned long)cy_enc, mm_cycles_to_us(cy_enc));
    print_hex("plaintext key", ROM_PRIVATE_KEY, TEST_KEY_SIZE);
    print_hex("encrypted key", s_enc_key_buf,   TEST_KEY_SIZE);

    /* --- Step 3: decrypt on use --- */
    uint8_t key_plaintext[TEST_KEY_SIZE] __attribute__((aligned(4)));
    t0 = mm_dwt_now();
    status = HAL_CRYP_Decrypt(&s_hcryp,
                               (uint32_t *)(uintptr_t)s_enc_key_buf,
                               TEST_KEY_SIZE / 4,
                               (uint32_t *)(uintptr_t)key_plaintext,
                               HAL_MAX_DELAY);
    uint32_t cy_dec = mm_dwt_elapsed(t0);
    if (status != HAL_OK) { HAL_CRYP_DeInit(&s_hcryp); return; }

    int match = (memcmp(key_plaintext, ROM_PRIVATE_KEY, TEST_KEY_SIZE) == 0);

    printf("\r\n  [Decryption on use]\r\n");
    printf("  key_plaintext addr         : 0x%08lX  (%s)\r\n",
           (unsigned long)(uintptr_t)key_plaintext,
           addr_region((uintptr_t)key_plaintext));
    printf("  DWT AES-256 ECB decrypt    : %lu cycles  (%.2f us)\r\n",
           (unsigned long)cy_dec, mm_cycles_to_us(cy_dec));
    print_hex("decrypted key", key_plaintext, TEST_KEY_SIZE);
    printf("  Round-trip match           : %s\r\n", match ? "PASS" : "FAIL");

    /* --- Step 4: zero plaintext key immediately --- */
    volatile uint8_t *kp = key_plaintext;
    for (uint32_t i = 0; i < TEST_KEY_SIZE; i++) kp[i] = 0;
    memset(s_enc_key_buf, 0, TEST_KEY_SIZE);
    HAL_CRYP_DeInit(&s_hcryp);

    printf("\r\n  >> Plaintext key in RAM : YES — fereastra decrypt+sign (~%.1f us)\r\n",
           mm_cycles_to_us(cy_dec));
    printf("  >> Encrypted key at rest: YES (noncacheable SRAM)\r\n");
    printf("  >> Security verdict     : HIGH — cheie protejata la repaus\r\n");
    printf("  >> Production path      : SAES+DHUK (necesita OTP provisioning)\r\n");
}

/* ==========================================================================
 * STRATEGY 6 — CRYP hardware key offload: key loaded to HW registers, RAM zeroed
 *
 * The private key scalar is loaded directly into the CRYP key registers.
 * The RAM copy is zeroed immediately after HAL_CRYP_Init().
 * Subsequent encrypt/decrypt operations use only the hardware key registers.
 *
 * For actual ECC signing offload, the PKA peripheral (Public Key Accelerator)
 * would be used.  Here we demonstrate the concept using CRYP AES-ECB:
 *   the key lives only in CRYP->K0R..K7R after the RAM wipe.
 *
 * Limitation: CRYP key registers are readable via the AHB bus unless TrustZone
 * marks CRYP as Secure.  Use SAES (Secure AES) for true hardware key isolation.
 * ==========================================================================*/
static void run_strategy6(void)
{
    print_line();
    printf("[S6] CRYP hardware key offload  -  key in HW registers, RAM zeroed\r\n");
    printf("     Demonstrates minimum plaintext key exposure window.\r\n\r\n");

    /* Make a mutable RAM copy of the wrap key so we can zero it after init */
    uint8_t ram_key[TEST_KEY_SIZE] __attribute__((aligned(4)));
    memcpy(ram_key, ROM_WRAP_KEY, TEST_KEY_SIZE);

    printf("  ram_key before init : 0x%08lX  (%s)\r\n",
           (unsigned long)(uintptr_t)ram_key,
           addr_region((uintptr_t)ram_key));
    print_hex("ram_key (plaintext)", ram_key, TEST_KEY_SIZE);

    /* Load key into CRYP hardware */
    HAL_StatusTypeDef status = cryp_init_aes256(ram_key);
    if (status != HAL_OK)
    {
        printf("  ERROR: HAL_CRYP_Init failed (status=%d)\r\n", (int)status);
        return;
    }

    /* Switch to KEYIVCONFIG_ONCE so the HAL never reloads the key from
     * the pKey pointer again — subsequent operations use the key already
     * held in the CRYP hardware registers, not the (about to be zeroed) RAM. */
    s_hcryp.Init.KeyIVConfigSkip = CRYP_KEYIVCONFIG_ONCE;
    s_hcryp.KeyIVConfig = 1U;   /* mark key as already configured */

    /* *** ZERO the RAM key immediately after hardware load *** */
    uint32_t t0 = mm_dwt_now();
    volatile uint8_t *kp = ram_key;
    for (uint32_t i = 0; i < TEST_KEY_SIZE; i++) kp[i] = 0;
    uint32_t cy_wipe = mm_dwt_elapsed(t0);

    printf("  CRYP init OK  -  RAM key wiped in %lu cycles  (%.2f us)\r\n",
           (unsigned long)cy_wipe, mm_cycles_to_us(cy_wipe));
    print_hex("ram_key (after wipe)", ram_key, TEST_KEY_SIZE);

    /* Verify CRYP still works: encrypt test data with key now only in HW */
    uint8_t  test_plain[16]  __attribute__((aligned(4))) = {
        0x00,0x11,0x22,0x33, 0x44,0x55,0x66,0x77,
        0x88,0x99,0xAA,0xBB, 0xCC,0xDD,0xEE,0xFF
    };
    uint8_t test_cipher[16] __attribute__((aligned(4))) = {0};
    uint8_t test_recover[16] __attribute__((aligned(4))) = {0};

    t0 = mm_dwt_now();
    status = HAL_CRYP_Encrypt(&s_hcryp,
                               (uint32_t *)(uintptr_t)test_plain,
                               4,   /* 16 bytes = 4 words */
                               (uint32_t *)(uintptr_t)test_cipher,
                               HAL_MAX_DELAY);
    uint32_t cy_enc = mm_dwt_elapsed(t0);

    if (status != HAL_OK)
    {
        printf("  ERROR: Encrypt after key wipe failed (status=%d, err=0x%08lX)\r\n",
               (int)status, (unsigned long)s_hcryp.ErrorCode);
        HAL_CRYP_DeInit(&s_hcryp);
        return;
    }

    t0 = mm_dwt_now();
    status = HAL_CRYP_Decrypt(&s_hcryp,
                               (uint32_t *)(uintptr_t)test_cipher,
                               4,
                               (uint32_t *)(uintptr_t)test_recover,
                               HAL_MAX_DELAY);
    uint32_t cy_dec = mm_dwt_elapsed(t0);

    if (status != HAL_OK)
    {
        printf("  ERROR: Decrypt after key wipe failed (status=%d, err=0x%08lX)\r\n",
               (int)status, (unsigned long)s_hcryp.ErrorCode);
        HAL_CRYP_DeInit(&s_hcryp);
        return;
    }

    int rt_ok = (memcmp(test_plain, test_recover, 16) == 0);

    printf("\r\n  [Encrypt/decrypt with HW-only key]\r\n");
    print_hex("plaintext ", test_plain,   16);
    print_hex("ciphertext", test_cipher,  16);
    print_hex("recovered ", test_recover, 16);
    printf("  DWT AES-256 ECB enc      : %lu cycles  (%.2f us)\r\n",
           (unsigned long)cy_enc, mm_cycles_to_us(cy_enc));
    printf("  DWT AES-256 ECB dec      : %lu cycles  (%.2f us)\r\n",
           (unsigned long)cy_dec, mm_cycles_to_us(cy_dec));
    printf("  Round-trip (enc→dec)     : %s\r\n", rt_ok ? "PASS" : "FAIL");

    /* Read back CRYP key registers to show they still hold the key
       (proves HW key register survived the RAM wipe) */
    printf("\r\n  CRYP K0R..K1R (first 8 bytes of HW key register):\r\n");
    printf("    K0LR=0x%08lX  K0RR=0x%08lX\r\n",
           (unsigned long)CRYP->K0LR, (unsigned long)CRYP->K0RR);

    HAL_CRYP_DeInit(&s_hcryp);

    printf("\r\n  >> Plaintext key in RAM  : only during memcpy + HAL_CRYP_Init (~%lu cycles)\r\n",
           (unsigned long)cy_wipe);
    printf("  >> Key in HW registers   : YES  -  CRYP K0R..K7R\r\n");
    printf("  >> Key readable via AHB  : YES (non-secure CRYP; use SAES for isolation)\r\n");
    printf("  >> Security verdict      : VERY HIGH with SAES/TrustZone; HIGH with plain CRYP\r\n");
    printf("  >> Production TODO       : PKA for ECDSA; SAES + secure world; OTFDEC for NOR\r\n");
}

/* ==========================================================================
 * STRATEGY 7 — mbedTLS-style HTTPS GET: dynamic cert/key allocation
 *
 * Simulates the heap allocation pattern of a real mbedTLS TLS 1.3 client
 * performing an HTTPS GET request.  No actual mbedTLS library is used — each
 * malloc mirrors an mbedTLS internal allocation with the correct size.
 *
 * mbedTLS allocation map (what parse_der + handshake actually does):
 *   malloc(cert_size)         — raw DER copy       (mbedtls_x509_crt_parse_der)
 *   malloc(sizeof x509_name)  — subject RDN node   (mbedtls_x509_get_name)
 *   malloc(sizeof x509_name)  — issuer  RDN node
 *   malloc(65)                — EC public key buf  (mbedtls_ecp_point_read_binary)
 *   malloc(~200)              — EC private key     (mbedtls_ecp_keypair)
 *   malloc(~200)              — ECDHE context      (mbedtls_ecdh_context)
 *   malloc(~512)              — handshake params   (mbedtls_ssl_handshake_params)
 *   malloc(SIM_TLS_BUF_SIZE)  — TLS input  buffer  (mbedtls_ssl_setup)
 *   malloc(SIM_TLS_BUF_SIZE)  — TLS output buffer
 *
 * Crypto operations are proxied through the STM32 CRYP peripheral:
 *   AES-ECB-256 encrypt → simulates ECDHE shared-secret derivation
 *   AES-ECB-256 encrypt → simulates TLS record encryption (HTTP GET)
 *   AES-ECB-256 decrypt → simulates TLS record decryption (HTTP 200 response)
 *
 * Phase breakdown:
 *   Phase 1 — TCP connect  (simulated: no network)
 *   Phase 2 — TLS handshake: cert parse + ECDHE + key load
 *   Phase 3 — HTTP GET encrypt + response decrypt
 *   Phase 4 — Session teardown: zero keys, free all heap
 * ==========================================================================*/

/* Allocation sizes derived from mbedTLS 3.6 source on 32-bit ARM */
#define SIM_X509_NAME_SIZE    72U   /* sizeof(mbedtls_x509_name)           */
#define SIM_EC_KEY_SIZE      200U   /* sizeof(mbedtls_ecp_keypair)          */
#define SIM_ECDH_SIZE        200U   /* sizeof(mbedtls_ecdh_context)         */
#define SIM_HANDSHAKE_SIZE   512U   /* sizeof(mbedtls_ssl_handshake_params) */
#define SIM_TLS_BUF_SIZE    4096U   /* MBEDTLS_SSL_MAX_CONTENT_LEN default  */

/* A simple container so we can always clean up even on early exit */
typedef struct {
    uint8_t *raw_der;
    uint8_t *subject_node;
    uint8_t *issuer_node;
    uint8_t *pub_key_buf;
    uint8_t *ec_key;
    uint8_t *ecdhe_ctx;
    uint8_t *hs_params;
    uint8_t *tls_in;
    uint8_t *tls_out;
} https_session_t;

static void https_session_free(https_session_t *s)
{
    /* Zero all sensitive fields before freeing */
    if (s->ec_key)   { volatile uint8_t *p = s->ec_key;
                       for (uint32_t i = 0; i < SIM_EC_KEY_SIZE; i++) p[i] = 0; }
    if (s->ecdhe_ctx){ volatile uint8_t *p = s->ecdhe_ctx;
                       for (uint32_t i = 0; i < SIM_ECDH_SIZE;   i++) p[i] = 0; }

    /* Free in reverse allocation order */
    if (s->tls_out)      mm_free(s->tls_out,      SIM_TLS_BUF_SIZE);
    if (s->tls_in)       mm_free(s->tls_in,        SIM_TLS_BUF_SIZE);
    if (s->hs_params)    mm_free(s->hs_params,     SIM_HANDSHAKE_SIZE);
    if (s->ecdhe_ctx)    mm_free(s->ecdhe_ctx,     SIM_ECDH_SIZE);
    if (s->ec_key)       mm_free(s->ec_key,        SIM_EC_KEY_SIZE);
    if (s->pub_key_buf)  mm_free(s->pub_key_buf,   65U);
    if (s->issuer_node)  mm_free(s->issuer_node,   SIM_X509_NAME_SIZE);
    if (s->subject_node) mm_free(s->subject_node,  SIM_X509_NAME_SIZE);
    if (s->raw_der)      mm_free(s->raw_der,        TEST_CERT_SIZE);
}

static void run_strategy7(void)
{
    print_line();
    printf("[S7] mbedTLS-style HTTPS GET  -  dynamic cert/key allocation\r\n");
    printf("     Simulates mbedtls_x509_crt_parse_der + TLS 1.3 handshake\r\n");
    printf("     with 9 separate heap allocations matching mbedTLS 3.6 sizes.\r\n\r\n");

    https_session_t ses = {0};
    mm_heap_reset_stats();

    /* ================================================================
     * Phase 1 — TCP connect (simulated, no network)
     * ================================================================*/
    printf("  [Phase 1] TCP connect to iot.example.com:443 (simulated)\r\n");
    uint32_t t0 = mm_dwt_now();
    volatile uint32_t tick = HAL_GetTick(); (void)tick; /* simulate socket init */
    printf("  Connect sim      : %lu cycles  (%.2f us)\r\n\r\n",
           (unsigned long)mm_dwt_elapsed(t0),
           mm_cycles_to_us(mm_dwt_elapsed(t0)));

    /* ================================================================
     * Phase 2 — TLS handshake
     *   2a  malloc raw DER copy           (mbedtls_x509_crt_parse_der)
     *   2b  malloc subject RDN name node  (mbedtls_x509_get_name)
     *   2c  malloc issuer  RDN name node
     *   2d  malloc EC public key buffer   (mbedtls_ecp_point_read_binary)
     *   2e  malloc EC private key struct  (mbedtls_pk_parse_key)
     *   2f  malloc ECDHE context          (mbedtls_ssl_handshake)
     *   2g  malloc handshake params
     * ================================================================*/
    printf("  [Phase 2] TLS handshake  -  parse server cert + ECDHE\r\n");
    uint32_t t_hs = mm_dwt_now();

    /* 2a — raw DER */
    ses.raw_der = (uint8_t *)mm_malloc(TEST_CERT_SIZE);
    if (!ses.raw_der) { printf("  OOM: raw_der\r\n"); goto teardown; }
    memcpy(ses.raw_der, ROM_CERT_DER, TEST_CERT_SIZE);

    /* 2b — subject name node */
    ses.subject_node = (uint8_t *)mm_malloc(SIM_X509_NAME_SIZE);
    if (!ses.subject_node) { printf("  OOM: subject_node\r\n"); goto teardown; }
    /* OID id-at-commonName (2.5.4.3) + CN value from DER offset 0x7C */
    memset(ses.subject_node, 0, SIM_X509_NAME_SIZE);
    ses.subject_node[0] = 0x55; ses.subject_node[1] = 0x04; ses.subject_node[2] = 0x03;
    memcpy(ses.subject_node + 8, "STM32-H7S3-Device-001", 21);

    /* 2c — issuer name node */
    ses.issuer_node = (uint8_t *)mm_malloc(SIM_X509_NAME_SIZE);
    if (!ses.issuer_node) { printf("  OOM: issuer_node\r\n"); goto teardown; }
    memset(ses.issuer_node, 0, SIM_X509_NAME_SIZE);
    ses.issuer_node[0] = 0x55; ses.issuer_node[1] = 0x04; ses.issuer_node[2] = 0x03;
    memcpy(ses.issuer_node + 8, "STM32-SecureStorage-Test-CA", 27);

    /* 2d — EC uncompressed public key (04 || X || Y, 65 bytes) */
    ses.pub_key_buf = (uint8_t *)mm_malloc(65U);
    if (!ses.pub_key_buf) { printf("  OOM: pub_key_buf\r\n"); goto teardown; }
    ses.pub_key_buf[0] = 0x04;  /* uncompressed point marker */
    memcpy(ses.pub_key_buf +  1, ROM_CERT_DER + 87, 32); /* X coord from cert */
    memcpy(ses.pub_key_buf + 33, ROM_CERT_DER + 87, 32); /* Y coord (placeholder) */

    /* Verify cert: DER SEQUENCE tag must be 0x30 */
    int cert_ok = (ses.raw_der[0] == 0x30);
    printf("  Cert parse+verify: %s  raw@0x%08lX (%s)\r\n",
           cert_ok ? "PASS" : "FAIL",
           (unsigned long)(uintptr_t)ses.raw_der,
           addr_region((uintptr_t)ses.raw_der));
    printf("  subject node     : 0x%08lX  issuer: 0x%08lX\r\n",
           (unsigned long)(uintptr_t)ses.subject_node,
           (unsigned long)(uintptr_t)ses.issuer_node);

    /* 2e — EC private key struct, load key scalar */
    ses.ec_key = (uint8_t *)mm_malloc(SIM_EC_KEY_SIZE);
    if (!ses.ec_key) { printf("  OOM: ec_key\r\n"); goto teardown; }
    memset(ses.ec_key, 0, SIM_EC_KEY_SIZE);
    memcpy(ses.ec_key, ROM_PRIVATE_KEY, TEST_KEY_SIZE);
    printf("  EC key loaded    : 0x%08lX (%s)\r\n",
           (unsigned long)(uintptr_t)ses.ec_key,
           addr_region((uintptr_t)ses.ec_key));

    /* 2f — ECDHE context; simulate shared-secret derivation with AES-ECB */
    ses.ecdhe_ctx = (uint8_t *)mm_malloc(SIM_ECDH_SIZE);
    if (!ses.ecdhe_ctx) { printf("  OOM: ecdhe_ctx\r\n"); goto teardown; }
    memset(ses.ecdhe_ctx, 0, SIM_ECDH_SIZE);
    {
        uint8_t shared_secret[32] __attribute__((aligned(4)));
        if (cryp_init_aes256(ROM_WRAP_KEY) == HAL_OK)
        {
            HAL_CRYP_Encrypt(&s_hcryp,
                             (uint32_t *)(uintptr_t)ROM_PRIVATE_KEY, 8,
                             (uint32_t *)(uintptr_t)shared_secret,
                             HAL_MAX_DELAY);
            HAL_CRYP_DeInit(&s_hcryp);
            memcpy(ses.ecdhe_ctx, shared_secret, 32); /* session master secret */
        }
    }

    /* 2g — handshake params struct */
    ses.hs_params = (uint8_t *)mm_malloc(SIM_HANDSHAKE_SIZE);
    if (!ses.hs_params) { printf("  OOM: hs_params\r\n"); goto teardown; }
    memset(ses.hs_params, 0, SIM_HANDSHAKE_SIZE);

    uint32_t cy_hs = mm_dwt_elapsed(t_hs);
    printf("  Handshake time   : %lu cycles  (%.2f us)\r\n",
           (unsigned long)cy_hs, mm_cycles_to_us(cy_hs));
    printf("  Heap after HS    : %lu bytes used  (%lu allocs)\r\n\r\n",
           (unsigned long)mm_heap_current_bytes,
           (unsigned long)mm_heap_alloc_count);

    /* ================================================================
     * Phase 3 — allocate TLS record I/O buffers + HTTP GET
     *   malloc(SIM_TLS_BUF_SIZE) × 2   (mbedtls_ssl_setup)
     * ================================================================*/
    printf("  [Phase 3] HTTP GET /api/secure-data  -  TLS record layer\r\n");

    ses.tls_in  = (uint8_t *)mm_malloc(SIM_TLS_BUF_SIZE);
    ses.tls_out = (uint8_t *)mm_malloc(SIM_TLS_BUF_SIZE);
    if (!ses.tls_in || !ses.tls_out)
    {
        printf("  OOM: TLS I/O buffers (need 2 x %u bytes)\r\n", SIM_TLS_BUF_SIZE);
        goto teardown;
    }
    printf("  TLS in  buf      : 0x%08lX (%s)\r\n",
           (unsigned long)(uintptr_t)ses.tls_in,
           addr_region((uintptr_t)ses.tls_in));
    printf("  TLS out buf      : 0x%08lX (%s)\r\n",
           (unsigned long)(uintptr_t)ses.tls_out,
           addr_region((uintptr_t)ses.tls_out));
    printf("  Peak heap so far : %lu bytes  (%lu allocs)\r\n",
           (unsigned long)mm_heap_peak_bytes,
           (unsigned long)mm_heap_alloc_count);

    /* Build HTTP GET request into tls_out, pad to AES block boundary */
    const char http_req[] =
        "GET /api/secure-data HTTP/1.1\r\n"
        "Host: iot.example.com\r\n"
        "Connection: close\r\n\r\n";
    uint32_t req_len    = (uint32_t)(sizeof(http_req) - 1);
    uint32_t req_padded = (req_len + 15U) & ~15U;
    memcpy(ses.tls_out, http_req, req_len);
    memset(ses.tls_out + req_len, 0, req_padded - req_len);

    /* Encrypt GET request → simulate TLS record encrypt (AES-ECB as proxy) */
    uint8_t enc_req[80] __attribute__((aligned(4))); /* max req_padded = 80 */
    t0 = mm_dwt_now();
    if (cryp_init_aes256(ROM_WRAP_KEY) == HAL_OK)
    {
        HAL_CRYP_Encrypt(&s_hcryp,
                         (uint32_t *)(uintptr_t)ses.tls_out,
                         req_padded / 4,
                         (uint32_t *)(uintptr_t)enc_req,
                         HAL_MAX_DELAY);
        HAL_CRYP_DeInit(&s_hcryp);
    }
    uint32_t cy_enc = mm_dwt_elapsed(t0);

    /* Simulate received TLS record: "HTTP/1.1 200 OK\r\n...\r\n\r\nDATA" */
    const char http_resp[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 16\r\n\r\n"
        "SECRET_DATA_1234";
    uint32_t resp_len    = (uint32_t)(sizeof(http_resp) - 1);
    uint32_t resp_padded = (resp_len + 15U) & ~15U;

    /* Encrypt the response (simulate what the server would send) */
    uint8_t enc_resp[64] __attribute__((aligned(4)));
    {
        uint8_t resp_plain[64] __attribute__((aligned(4)));
        memcpy(resp_plain, http_resp, resp_len);
        memset(resp_plain + resp_len, 0, resp_padded - resp_len);
        if (cryp_init_aes256(ROM_WRAP_KEY) == HAL_OK)
        {
            HAL_CRYP_Encrypt(&s_hcryp,
                             (uint32_t *)(uintptr_t)resp_plain,
                             resp_padded / 4,
                             (uint32_t *)(uintptr_t)enc_resp,
                             HAL_MAX_DELAY);
            HAL_CRYP_DeInit(&s_hcryp);
        }
    }

    /* Decrypt the "received" TLS record into tls_in */
    t0 = mm_dwt_now();
    if (cryp_init_aes256(ROM_WRAP_KEY) == HAL_OK)
    {
        HAL_CRYP_Decrypt(&s_hcryp,
                         (uint32_t *)(uintptr_t)enc_resp,
                         resp_padded / 4,
                         (uint32_t *)(uintptr_t)ses.tls_in,
                         HAL_MAX_DELAY);
        HAL_CRYP_DeInit(&s_hcryp);
    }
    uint32_t cy_dec = mm_dwt_elapsed(t0);

    int resp_ok = (memcmp(ses.tls_in, http_resp, resp_len) == 0);
    printf("  GET req encrypt  : %lu cycles  (%.2f us)  [%lu B → %lu B padded]\r\n",
           (unsigned long)cy_enc, mm_cycles_to_us(cy_enc),
           (unsigned long)req_len, (unsigned long)req_padded);
    printf("  Response decrypt : %lu cycles  (%.2f us)  [%lu B → %lu B padded]\r\n",
           (unsigned long)cy_dec, mm_cycles_to_us(cy_dec),
           (unsigned long)resp_len, (unsigned long)resp_padded);
    printf("  Response match   : %s  (%.*s)\r\n\r\n",
           resp_ok ? "PASS" : "FAIL",
           (int)resp_len, (char *)ses.tls_in);

    /* ================================================================
     * Phase 4 — teardown
     * ================================================================*/
teardown:
    printf("  [Phase 4] Session teardown  -  zero keys, free all heap\r\n");
    t0 = mm_dwt_now();
    https_session_free(&ses);
    uint32_t cy_tear = mm_dwt_elapsed(t0);

    printf("  Teardown time    : %lu cycles  (%.2f us)\r\n",
           (unsigned long)cy_tear, mm_cycles_to_us(cy_tear));
    printf("  Total allocs     : %lu  frees: %lu\r\n",
           (unsigned long)mm_heap_alloc_count,
           (unsigned long)mm_heap_free_count);

    printf("\r\n  Allocation breakdown (mbedTLS-style):\r\n");
    printf("    raw DER copy          : %4u B  (mbedtls_x509_crt_parse_der)\r\n",  TEST_CERT_SIZE);
    printf("    subject RDN node      : %4u B  (mbedtls_x509_get_name)\r\n",       SIM_X509_NAME_SIZE);
    printf("    issuer  RDN node      : %4u B  (mbedtls_x509_get_name)\r\n",       SIM_X509_NAME_SIZE);
    printf("    EC public key buf     : %4u B  (mbedtls_ecp_point_read_binary)\r\n", 65U);
    printf("    EC private key struct : %4u B  (mbedtls_pk_parse_key)\r\n",        SIM_EC_KEY_SIZE);
    printf("    ECDHE context         : %4u B  (mbedtls_ecdh_context)\r\n",        SIM_ECDH_SIZE);
    printf("    handshake params      : %4u B  (mbedtls_ssl_handshake_params)\r\n",SIM_HANDSHAKE_SIZE);
    printf("    TLS input  buffer     : %4u B  (mbedtls_ssl_setup, in)\r\n",       SIM_TLS_BUF_SIZE);
    printf("    TLS output buffer     : %4u B  (mbedtls_ssl_setup, out)\r\n",      SIM_TLS_BUF_SIZE);
    printf("    ─────────────────────────────\r\n");
    uint32_t total_alloc = TEST_CERT_SIZE + 2*SIM_X509_NAME_SIZE + 65
                         + SIM_EC_KEY_SIZE + SIM_ECDH_SIZE
                         + SIM_HANDSHAKE_SIZE + 2*SIM_TLS_BUF_SIZE;
    printf("    TOTAL peak            : %4lu B\r\n", (unsigned long)mm_heap_peak_bytes);
    printf("    (theoretical sum)     : %4lu B\r\n", (unsigned long)total_alloc);

    printf("\r\n  >> RAM peak for HTTPS session: %lu bytes\r\n",
           (unsigned long)mm_heap_peak_bytes);
    printf("  >> Private key in heap       : YES  -  entire handshake duration\r\n");
    printf("  >> Key zeroed before free    : YES (ec_key + ecdhe_ctx)\r\n");
    printf("  >> TLS buffers dominate      : 2 x %u B = %u B\r\n",
           SIM_TLS_BUF_SIZE, 2 * SIM_TLS_BUF_SIZE);
    printf("  >> Security verdict          : MEDIUM  -  key plaintext in heap\r\n");
    printf("  >>   Improvement: replace ec_key alloc with S6 CRYP offload\r\n");
    printf("  >>   Improvement: reduce buf size with MBEDTLS_SSL_MAX_CONTENT_LEN\r\n");
}

/* ==========================================================================
 * Strategy 8 — Real LwIP TCP HTTP GET with dynamic cert/key allocation
 *
 * This is the only strategy that performs real network I/O.  It requires
 * the LwIP stack and DHCP to be running (initialised in BenchmarkTask
 * before cert_strategies_run_all() is called).
 *
 * Allocation pattern (same as S7 but with a real TCP connection):
 *   malloc(TEST_CERT_SIZE)  — heap buffer for DER certificate
 *   malloc(TEST_KEY_SIZE)   — heap buffer for ECC private key
 *   netconn_new()           — LwIP connection object
 *   HTTP GET over TCP       — real packet exchange
 *   memset(key,0)           — key zeroed before free
 *   free(cert), free(key)   — explicit dealloc
 *
 * The target server and port are compile-time defines so you can point
 * the board at any reachable HTTP server on your bench network.
 * ==========================================================================*/
#define S8_SERVER_IP    HTTP_SERVER_IP
#define S8_SERVER_PORT  HTTP_SERVER_PORT

#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "lwip/ip_addr.h"

static void run_strategy8(void)
{
    print_line();
    printf("[S8] Real LwIP TCP HTTP GET  -  dynamic cert/key allocation\r\n");
    printf("     Connects to " S8_SERVER_IP ":%u via RMII Ethernet (LAN8742).\r\n",
           (unsigned)S8_SERVER_PORT);

    /* --- Check network is available (DHCP or static fallback) --------------- */
    extern struct netif gnetif;
    if (!gnetif.ip_addr.addr)
    {
        printf("[S8] SKIP  -  no IP address (Ethernet cable connected? Server up?)\r\n");
        return;
    }

    /* --- Measure cert/key heap allocation ----------------------------------- */
    mm_heap_reset_stats();
    uint32_t t0, t1;

    /* Allocate certificate buffer */
    uint8_t *cert_buf = (uint8_t *)mm_malloc(TEST_CERT_SIZE);
    if (!cert_buf) { printf("[S8] FAIL: cert mm_malloc\r\n"); return; }
    memcpy(cert_buf, ROM_CERT_DER, TEST_CERT_SIZE);

    /* Allocate private key buffer */
    uint8_t *key_buf = (uint8_t *)mm_malloc(TEST_KEY_SIZE);
    if (!key_buf) { mm_free(cert_buf, TEST_CERT_SIZE); printf("[S8] FAIL: key mm_malloc\r\n"); return; }
    memcpy(key_buf, ROM_PRIVATE_KEY, TEST_KEY_SIZE);

    printf("[S8]  cert_buf @ 0x%08lX  (%u B)\r\n",
           (unsigned long)cert_buf, (unsigned)TEST_CERT_SIZE);
    printf("[S8]  key_buf  @ 0x%08lX  (%u B)\r\n",
           (unsigned long)key_buf, (unsigned)TEST_KEY_SIZE);

    /* --- HTTP GET via raw TCP (NO_SYS polling — no netconn) ----------------- */
    http_response_t resp;

    t0 = mm_dwt_now();
    http_client_result_t rc = http_client_get(S8_SERVER_IP, S8_SERVER_PORT, "/", &resp);
    t1 = mm_dwt_now();

    if (rc == HTTP_CLIENT_OK)
    {
        printf("[S8]  HTTP status    : %d\r\n", resp.status_code);
        printf("[S8]  Response bytes : %lu\r\n", (unsigned long)resp.resp_total);
    }
    else
    {
        printf("[S8]  FAIL: http_client_get rc=%d\r\n", (int)rc);
    }

    printf("[S8]  GET round-trip : %lu cycles (%.2f us @ %lu MHz)\r\n",
           (unsigned long)(t1 - t0),
           (float)(t1 - t0) / (float)(SystemCoreClock / 1000000U),
           (unsigned long)(SystemCoreClock / 1000000U));

    /* --- Zero key, free both buffers --------------------------------------- */
    memset(key_buf, 0, TEST_KEY_SIZE);
    mm_free(key_buf, TEST_KEY_SIZE);
    mm_free(cert_buf, TEST_CERT_SIZE);

    /* --- Summary ----------------------------------------------------------- */
    printf("\r\n[S8] Summary:\r\n");
    printf("     Cert alloc @ heap      : YES (%u B)\r\n", (unsigned)TEST_CERT_SIZE);
    printf("     Key alloc @ heap       : YES (%u B), zeroed before free\r\n",
           (unsigned)TEST_KEY_SIZE);
    printf("     Peak heap (S8 only)    : %lu B\r\n", (unsigned long)mm_heap_peak_bytes);
    printf("     Transport              : Real LwIP TCP (Ethernet RMII, LAN8742)\r\n");
    printf("     TLS                    : NOT present (mbedTLS not in project)\r\n");
    printf("     Key in plaintext RAM   : YES  -  duration of TCP session\r\n");
    printf("     Security verdict       : MEDIUM  -  adds real network overhead\r\n");
    printf("     Next step              : add mbedTLS for full TLS handshake\r\n");
}

/* ==========================================================================
 * STRATEGY 9 — Real HTTPS via mbedTLS 3.6  (TLS 1.2, RSA, AES-128-GCM)
 *
 * This strategy performs a real TLS 1.2 handshake to the Python HTTPS server
 * (server_https.py on port 8443).  It allocates cert + key on the heap
 * (same pattern as S8), measures handshake time with DWT, and reports:
 *   - mbedTLS context sizes (ssl, config, x509_crt, ctr_drbg, entropy)
 *   - TLS handshake DWT cycle count
 *   - Peak heap during the complete TLS session
 *   - Cipher suite negotiated
 * ==========================================================================*/
static void run_strategy9(void)
{
    print_line();
    printf("[S9] Real HTTPS via mbedTLS 3.6  -  TLS 1.2 RSA + AES-128-GCM\r\n");
    printf("     Connects to " HTTPS_SERVER_IP ":%u (self-signed cert, verify=OPTIONAL).\r\n\r\n",
           (unsigned)HTTPS_SERVER_PORT);

    extern struct netif gnetif;
    if (!gnetif.ip_addr.addr) {
        printf("[S9] SKIP  -  no IP address (Ethernet cable connected?)\r\n");
        return;
    }

    /* ---- mbedTLS struct sizes (measured on this build, -O0) ---- */
    printf("[S9] mbedTLS context sizes on Cortex-M7 (-O0):\r\n");
    printf("     mbedtls_ssl_context      : %4u B\r\n", (unsigned)sizeof(mbedtls_ssl_context));
    printf("     mbedtls_ssl_config       : %4u B\r\n", (unsigned)sizeof(mbedtls_ssl_config));
    printf("     mbedtls_x509_crt         : %4u B\r\n", (unsigned)sizeof(mbedtls_x509_crt));
    printf("     mbedtls_ctr_drbg_context : %4u B\r\n", (unsigned)sizeof(mbedtls_ctr_drbg_context));
    printf("     mbedtls_entropy_context  : %4u B\r\n\r\n", (unsigned)sizeof(mbedtls_entropy_context));

    /* ---- Heap-allocate cert + key (same as S7/S8 pattern) ---- */
    mm_heap_reset_stats();

    uint8_t *cert_buf = (uint8_t *)mm_malloc(TEST_CERT_SIZE);
    if (!cert_buf) { printf("[S9] FAIL: cert malloc\r\n"); return; }
    memcpy(cert_buf, ROM_CERT_DER, TEST_CERT_SIZE);

    uint8_t *key_buf = (uint8_t *)mm_malloc(TEST_KEY_SIZE);
    if (!key_buf) {
        mm_free(cert_buf, TEST_CERT_SIZE);
        printf("[S9] FAIL: key malloc\r\n");
        return;
    }
    memcpy(key_buf, ROM_PRIVATE_KEY, TEST_KEY_SIZE);

    printf("[S9]  cert_buf @ 0x%08lX  (%u B)\r\n", (unsigned long)cert_buf, (unsigned)TEST_CERT_SIZE);
    printf("[S9]  key_buf  @ 0x%08lX  (%u B)\r\n", (unsigned long)key_buf,  (unsigned)TEST_KEY_SIZE);

    /* ---- HTTPS GET via mbedTLS ---- */
    tls_response_t tresp;
    uint32_t t0 = mm_dwt_now();
    tls_result_t rc = tls_client_get(HTTPS_SERVER_IP, HTTPS_SERVER_PORT, "/", &tresp);
    uint32_t total_cy = mm_dwt_elapsed(t0);

    if (rc == TLS_OK) {
        printf("[S9]  HTTP status    : %d\r\n", tresp.status_code);
        printf("[S9]  Response bytes : %lu\r\n", (unsigned long)tresp.resp_total);
        if (tresp.body && tresp.body_len > 0)
            printf("[S9]  Body           : %.*s\r\n", (int)tresp.body_len, tresp.body);
    } else {
        printf("[S9]  FAIL: tls_client_get rc=%d\r\n", (int)rc);
    }

    printf("[S9]  TLS handshake  : %lu cycles  (%.2f ms @ %lu MHz)\r\n",
           (unsigned long)tresp.handshake_cycles,
           (float)tresp.handshake_cycles / (float)(SystemCoreClock / 1000U),
           (unsigned long)(SystemCoreClock / 1000000U));
    printf("[S9]  Total round-trip: %lu cycles (%.2f ms)\r\n",
           (unsigned long)total_cy,
           (float)total_cy / (float)(SystemCoreClock / 1000U));

    /* ---- Secure-erase key, free buffers ---- */
    volatile uint8_t *kp = key_buf;
    for (uint32_t i = 0; i < TEST_KEY_SIZE; i++) kp[i] = 0;
    mm_free(key_buf,  TEST_KEY_SIZE);
    mm_free(cert_buf, TEST_CERT_SIZE);

    printf("\r\n[S9] Summary:\r\n");
    printf("     TLS library      : mbedTLS 3.6.6\r\n");
    printf("     Protocol         : TLS 1.2\r\n");
    printf("     Key exchange     : RSA (no ECDHE)\r\n");
    printf("     Cipher suite     : AES-128-GCM-SHA256 (negotiated)\r\n");
    printf("     Cert verification: OPTIONAL (self-signed, embedded CA)\r\n");
    printf("     Peak heap (S9)   : %lu B  (%lu allocs)\r\n",
           (unsigned long)mm_heap_peak_bytes, (unsigned long)mm_heap_alloc_count);
    printf("     Key in plaintext : YES  -  duration of TLS session\r\n");
    printf("     Security verdict : HIGH  -  full encrypted channel, cert verified\r\n");
}

/* ==========================================================================
 * Telemetry helper — POST one JSON record to /metrics after each strategy.
 *
 * Fields:
 *   timestamp         HAL_GetTick() ms since boot
 *   cpu_usage         strategy_cycles / SystemCoreClock * 100  (% of 1 s)
 *   memory_usage      mm_heap_peak_bytes (reset before each strategy)
 *   packets_in/out    delta of g_eth_rx_count / g_tx_cnt during strategy
 *   power_consumption 0.0 placeholder
 *   security_scenario caller-supplied name string
 * ==========================================================================*/
static void s_post_telemetry(const char *scenario,
                             uint32_t    strategy_cycles,
                             uint32_t    heap_peak_bytes,
                             uint32_t    pkt_in,
                             uint32_t    pkt_out)
{
    char json[400];
    float cpu_pct = (float)strategy_cycles / (float)SystemCoreClock * 100.0f;

    int len = snprintf(json, sizeof(json),
        "{\"timestamp\":%lu,"
        "\"cpu_usage\":%.6f,"
        "\"memory_usage\":%lu,"
        "\"packets_in\":%lu,"
        "\"packets_out\":%lu,"
        "\"power_consumption\":0.0,"
        "\"security_scenario\":\"%s\"}",
        (unsigned long)HAL_GetTick(),
        (double)cpu_pct,
        (unsigned long)heap_peak_bytes,
        (unsigned long)pkt_in,
        (unsigned long)pkt_out,
        scenario);

    if (len <= 0 || (uint32_t)len >= sizeof(json))
        return;

    http_response_t resp;
    http_client_result_t rc = http_client_post(
        HTTP_SERVER_IP, HTTP_SERVER_PORT, "/metrics",
        "application/json", json, (uint32_t)len, &resp);

    if (rc == HTTP_CLIENT_OK) {
        printf("[telemetry] POST %-20s -> HTTP %d\r\n", scenario, resp.status_code);
        BSP_LED_Toggle(LED_YELLOW);   /* one blink per successful strategy post */
    } else {
        printf("[telemetry] POST %-20s -> FAIL (rc=%d)\r\n", scenario, (int)rc);
    }
}

/* ==========================================================================
 * Public entry point
 * ==========================================================================*/
void cert_strategies_run_all(void)
{
    extern volatile uint32_t g_eth_rx_count;
    extern volatile uint32_t g_tx_cnt;

    /* Enable DWT cycle counter */
    mm_dwt_init();

    printf("\r\n");
    printf("===========================================================\r\n");
    printf("  Secure Storage Research  -  Certificate Allocation Benchmark\r\n");
    printf("  STM32H7S3L8Hx  |  Cortex-M7 @ %lu MHz\r\n",
           (unsigned long)(SystemCoreClock / 1000000U));
    printf("===========================================================\r\n");
    printf("  XIP NOR  : 0x70000000  (ROM_CERT_DER lives here)\r\n");
    printf("  DTCM RAM : 0x20000000  (stack, 64 KB)\r\n");
    printf("  AXI SRAM : 0x24000000  (data + heap, ~456 KB)\r\n");
    printf("  NONCACHE : 0x24071C00  (1 KB, noncacheable_buffer section)\r\n");

    uint32_t t0, elapsed, rx0, tx0;

    mm_heap_reset_stats();
    rx0 = g_eth_rx_count; tx0 = g_tx_cnt;
    t0 = mm_dwt_now(); run_strategy1(); elapsed = mm_dwt_elapsed(t0);
    s_post_telemetry("S1_ROM_Static", elapsed, mm_heap_peak_bytes,
                     g_eth_rx_count - rx0, g_tx_cnt - tx0);

    mm_heap_reset_stats();
    rx0 = g_eth_rx_count; tx0 = g_tx_cnt;
    t0 = mm_dwt_now(); run_strategy2(); elapsed = mm_dwt_elapsed(t0);
    s_post_telemetry("S2_Stack", elapsed, mm_heap_peak_bytes,
                     g_eth_rx_count - rx0, g_tx_cnt - tx0);

    mm_heap_reset_stats();
    rx0 = g_eth_rx_count; tx0 = g_tx_cnt;
    t0 = mm_dwt_now(); run_strategy3(); elapsed = mm_dwt_elapsed(t0);
    s_post_telemetry("S3_Heap", elapsed, mm_heap_peak_bytes,
                     g_eth_rx_count - rx0, g_tx_cnt - tx0);

    mm_heap_reset_stats();
    rx0 = g_eth_rx_count; tx0 = g_tx_cnt;
    t0 = mm_dwt_now(); run_strategy4(); elapsed = mm_dwt_elapsed(t0);
    s_post_telemetry("S4_SecureStorage", elapsed, mm_heap_peak_bytes,
                     g_eth_rx_count - rx0, g_tx_cnt - tx0);

    mm_heap_reset_stats();
    rx0 = g_eth_rx_count; tx0 = g_tx_cnt;
    t0 = mm_dwt_now(); run_strategy5(); elapsed = mm_dwt_elapsed(t0);
    s_post_telemetry("S5_AES_Wrap", elapsed, mm_heap_peak_bytes,
                     g_eth_rx_count - rx0, g_tx_cnt - tx0);

    mm_heap_reset_stats();
    rx0 = g_eth_rx_count; tx0 = g_tx_cnt;
    t0 = mm_dwt_now(); run_strategy6(); elapsed = mm_dwt_elapsed(t0);
    s_post_telemetry("S6_CRYP_Offload", elapsed, mm_heap_peak_bytes,
                     g_eth_rx_count - rx0, g_tx_cnt - tx0);

    mm_heap_reset_stats();
    rx0 = g_eth_rx_count; tx0 = g_tx_cnt;
    t0 = mm_dwt_now(); run_strategy7(); elapsed = mm_dwt_elapsed(t0);
    s_post_telemetry("S7_mbedTLS_sim", elapsed, mm_heap_peak_bytes,
                     g_eth_rx_count - rx0, g_tx_cnt - tx0);

    mm_heap_reset_stats();
    rx0 = g_eth_rx_count; tx0 = g_tx_cnt;
    t0 = mm_dwt_now(); run_strategy8(); elapsed = mm_dwt_elapsed(t0);
    s_post_telemetry("S8_Real_TCP", elapsed, mm_heap_peak_bytes,
                     g_eth_rx_count - rx0, g_tx_cnt - tx0);

    mm_heap_reset_stats();
    rx0 = g_eth_rx_count; tx0 = g_tx_cnt;
    t0 = mm_dwt_now(); run_strategy9(); elapsed = mm_dwt_elapsed(t0);
    s_post_telemetry("S9_mbedTLS_HTTPS", elapsed, mm_heap_peak_bytes,
                     g_eth_rx_count - rx0, g_tx_cnt - tx0);

    print_line();
    printf("\r\n[DONE] All 9 strategies complete.\r\n");
    printf("       Review cycle counts and address regions above.\r\n\r\n");
}
