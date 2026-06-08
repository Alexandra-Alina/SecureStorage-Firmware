/*
 * mem_measure.c — DWT cycle counter + stack watermark + heap tracking
 *
 * STM32H7S3 @ 600 MHz, Cortex-M7 DWT.
 */
#include "mem_measure.h"
#include "stm32h7rsxx_hal.h"
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * DWT
 * ========================================================================= */

void mm_dwt_init(void)
{
    /* Unlock DWT */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT       = 0;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t mm_dwt_now(void)
{
    return DWT->CYCCNT;
}

uint32_t mm_dwt_elapsed(uint32_t t0)
{
    /* Works correctly even if CYCCNT wraps (32-bit unsigned subtraction). */
    return (DWT->CYCCNT - t0);
}

float mm_cycles_to_us(uint32_t cycles)
{
    /* Avoid division by zero on un-initialized clocks. */
    uint32_t clk = SystemCoreClock;
    if (clk == 0) clk = 600000000U;
    return (float)cycles / ((float)clk / 1e6f);
}

/* =========================================================================
 * Stack watermark
 * ========================================================================= */

void mm_stack_paint(void *base, size_t size)
{
    memset(base, STACK_FILL_BYTE, size);
}

uint32_t mm_stack_watermark(const void *base, size_t size)
{
    const uint8_t *p = (const uint8_t *)base;
    uint32_t used = 0;
    for (size_t i = 0; i < size; i++)
    {
        if (p[i] != STACK_FILL_BYTE) used++;
    }
    return used;
}

/* =========================================================================
 * Heap tracking shim
 * ========================================================================= */

uint32_t mm_heap_alloc_count   = 0;
uint32_t mm_heap_free_count    = 0;
uint32_t mm_heap_peak_bytes    = 0;
uint32_t mm_heap_current_bytes = 0;

void mm_heap_reset_stats(void)
{
    mm_heap_alloc_count   = 0;
    mm_heap_free_count    = 0;
    mm_heap_peak_bytes    = 0;
    mm_heap_current_bytes = 0;
}

void *mm_malloc(size_t size)
{
    void *p = malloc(size);
    if (p)
    {
        mm_heap_alloc_count++;
        mm_heap_current_bytes += (uint32_t)size;
        if (mm_heap_current_bytes > mm_heap_peak_bytes)
            mm_heap_peak_bytes = mm_heap_current_bytes;
    }
    return p;
}

void mm_free(void *ptr, size_t size)
{
    if (ptr)
    {
        free(ptr);
        mm_heap_free_count++;
        if (mm_heap_current_bytes >= (uint32_t)size)
            mm_heap_current_bytes -= (uint32_t)size;
        else
            mm_heap_current_bytes = 0;
    }
}
