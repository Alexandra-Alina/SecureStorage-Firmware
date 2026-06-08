#ifndef MEM_MEASURE_H
#define MEM_MEASURE_H

#include <stdint.h>
#include <stddef.h>

/* ---------------------------------------------------------------------------
 * DWT cycle-counter helpers (Cortex-M7, 600 MHz clock assumed).
 *
 * Usage:
 *   mm_dwt_init();
 *   uint32_t t0 = mm_dwt_now();
 *   ... work ...
 *   uint32_t cycles = mm_dwt_elapsed(t0);
 *   printf("%lu cycles / %.3f us\n", cycles, mm_cycles_to_us(cycles));
 * ---------------------------------------------------------------------------*/

/* Enable DWT and CYCCNT — call once after HAL_Init(). */
void     mm_dwt_init(void);

/* Read current CYCCNT value (wraps every ~7.2 s at 600 MHz). */
uint32_t mm_dwt_now(void);

/* Compute elapsed cycles from a previously captured t0 (handles wrap). */
uint32_t mm_dwt_elapsed(uint32_t t0);

/* Convert cycles to microseconds (uses SystemCoreClock). */
float    mm_cycles_to_us(uint32_t cycles);

/* ---------------------------------------------------------------------------
 * Stack watermark helpers.
 *
 * Paint a region with STACK_FILL_BYTE, then check how much was consumed.
 * Call mm_stack_paint() at function entry, mm_stack_watermark() at exit.
 * ---------------------------------------------------------------------------*/
#define STACK_FILL_BYTE 0xAA

/* Fill 'size' bytes starting at 'base' with STACK_FILL_BYTE. */
void     mm_stack_paint(void *base, size_t size);

/* Count bytes that were overwritten (no longer == STACK_FILL_BYTE). */
uint32_t mm_stack_watermark(const void *base, size_t size);

/* ---------------------------------------------------------------------------
 * Simple heap tracking shim (wraps malloc/free counters).
 * ---------------------------------------------------------------------------*/
extern uint32_t mm_heap_alloc_count;
extern uint32_t mm_heap_free_count;
extern uint32_t mm_heap_peak_bytes;
extern uint32_t mm_heap_current_bytes;

void    *mm_malloc(size_t size);
void     mm_free(void *ptr, size_t size);
void     mm_heap_reset_stats(void);

#endif /* MEM_MEASURE_H */
