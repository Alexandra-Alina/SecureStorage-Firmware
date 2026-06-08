/*
 * FreeRTOS Kernel V10.6.2
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 * SPDX-License-Identifier: MIT
 *
 * GCC / ARM Cortex-M4F port — also compatible with Cortex-M7 (ARMv7-M + FPUv5).
 * Used here for STM32H7S3L8Hx (Cortex-M7 r0p2).
 *
 * Note: TIM1 is used as the HAL timebase (stm32h7rsxx_hal_timebase_tim.c).
 *       SysTick is therefore free for FreeRTOS tick generation.
 */

/* Scheduler includes. */
#include "FreeRTOS.h"
#include "task.h"

#if !defined(__ARM_FP) && !defined(__VFP_FP__)
    #error This port requires hardware FPU (-mfloat-abi=hard -mfpu=fpv5-d16).
#endif

#if ( configMAX_SYSCALL_INTERRUPT_PRIORITY == 0 )
    #error configMAX_SYSCALL_INTERRUPT_PRIORITY must not be 0. See www.FreeRTOS.org/RTOS-Cortex-M3-M4.html
#endif

/* =========================================================================
 * Hardware register addresses
 * ========================================================================= */
#define portNVIC_SYSTICK_CTRL_REG          ( *( ( volatile uint32_t * ) 0xe000e010UL ) )
#define portNVIC_SYSTICK_LOAD_REG          ( *( ( volatile uint32_t * ) 0xe000e014UL ) )
#define portNVIC_SYSTICK_CURRENT_VALUE_REG ( *( ( volatile uint32_t * ) 0xe000e018UL ) )
#define portNVIC_SHPR3_REG                 ( *( ( volatile uint32_t * ) 0xe000ed20UL ) )

#define portNVIC_SYSTICK_CLK_BIT    ( 1UL << 2UL )
#define portNVIC_SYSTICK_INT_BIT    ( 1UL << 1UL )
#define portNVIC_SYSTICK_ENABLE_BIT ( 1UL << 0UL )
#define portMIN_INTERRUPT_PRIORITY  ( 255UL )
#define portNVIC_PENDSV_PRI         ( portMIN_INTERRUPT_PRIORITY << 16UL )
#define portNVIC_SYSTICK_PRI        ( portMIN_INTERRUPT_PRIORITY << 24UL )

/* FPU context control register — enable lazy stacking. */
#define portFPCCR                   ( *( ( volatile uint32_t * ) 0xe000ef34UL ) )
#define portASPEN_AND_LSPEN_BITS    ( 0x3UL << 30UL )

/* Initial XPSR: Thumb bit set. */
#define portINITIAL_XPSR            ( 0x01000000UL )
/* EXC_RETURN: return to Thread mode, use PSP, FPU not active. */
#define portINITIAL_EXC_RETURN      ( 0xFFFFFFFDUL )
/* Mask off bit 0 of PC (Thumb mode is implied). */
#define portSTART_ADDRESS_MASK      ( ( StackType_t ) 0xFFFFFFFEUL )

/* =========================================================================
 * Forward declarations (assembly functions defined below)
 * ========================================================================= */
static void prvStartFirstTask( void ) __attribute__( ( naked ) );
static void prvEnableVFP( void )      __attribute__( ( naked ) );
static void prvTaskExitError( void );

/* =========================================================================
 * Per-task nesting count (initialised to a sentinel to catch early use)
 * ========================================================================= */
static UBaseType_t uxCriticalNesting = 0xaaaaaaaaUL;

/* =========================================================================
 * pxPortInitialiseStack
 *
 * Sets up the initial stack frame for a newly created task so that when the
 * scheduler first switches to it, the CPU sees a valid exception return frame.
 *
 * Stack layout (descending, from high address):
 *   xPSR, PC, LR, R12, R3, R2, R1, R0   <- hardware-stacked on exception
 *   EXC_RETURN, R11..R4                  <- software-stacked by PendSV handler
 * ========================================================================= */
StackType_t *pxPortInitialiseStack( StackType_t *pxTopOfStack,
                                    TaskFunction_t pxCode,
                                    void *pvParameters )
{
    /* Simulate the exception frame that the CPU would push. */
    pxTopOfStack--;
    *pxTopOfStack = portINITIAL_XPSR;                           /* xPSR */
    pxTopOfStack--;
    *pxTopOfStack = ( ( StackType_t ) pxCode ) & portSTART_ADDRESS_MASK; /* PC */
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) prvTaskExitError;           /* LR */
    pxTopOfStack -= 5;  /* R12, R3, R2, R1 — zero-initialised by the caller */
    *pxTopOfStack = ( StackType_t ) pvParameters;               /* R0 */

    /* EXC_RETURN value placed where PendSV expects to find it (R14 slot). */
    pxTopOfStack--;
    *pxTopOfStack = portINITIAL_EXC_RETURN;

    /* R11 through R4. */
    pxTopOfStack -= 8;

    return pxTopOfStack;
}

/* =========================================================================
 * prvTaskExitError
 * Called if a task function ever returns (it should not).
 * ========================================================================= */
static void prvTaskExitError( void )
{
    volatile uint32_t ulDummy = 0UL;
    /* Force an assert. */
    configASSERT( ulDummy );
    for( ; ; ) {}
}

/* =========================================================================
 * vPortSVCHandler  (aliased as SVC_Handler in FreeRTOSConfig.h)
 * Starts the very first task by loading its saved context from pxCurrentTCB.
 * ========================================================================= */
void vPortSVCHandler( void ) __attribute__( ( naked ) );
void vPortSVCHandler( void )
{
    __asm volatile (
        "   ldr  r3, pxCurrentTCBConst2     \n"  /* r3 = &pxCurrentTCB        */
        "   ldr  r1, [r3]                   \n"  /* r1 = pxCurrentTCB          */
        "   ldr  r0, [r1]                   \n"  /* r0 = top-of-stack          */
        "   ldmia r0!, {r4-r11, r14}        \n"  /* pop R4-R11 and EXC_RETURN  */
        "   msr  psp, r0                    \n"  /* restore PSP                */
        "   isb                             \n"
        "   mov  r0, #0                     \n"
        "   msr  basepri, r0               \n"  /* enable all interrupts       */
        "   bx   r14                        \n"  /* return using EXC_RETURN     */
        "   .align 4                        \n"
        "pxCurrentTCBConst2: .word pxCurrentTCB \n"
    );
}

/* =========================================================================
 * prvStartFirstTask
 * Triggers SVC to transfer control to vPortSVCHandler.
 * ========================================================================= */
static void prvStartFirstTask( void )
{
    __asm volatile (
        /* Set MSP back to the initial stack pointer so SVC has a clean frame. */
        "   ldr  r0, =0xE000ED08            \n"  /* VTOR address               */
        "   ldr  r0, [r0]                   \n"  /* vector table base           */
        "   ldr  r0, [r0]                   \n"  /* initial MSP value           */
        "   msr  msp, r0                    \n"
        "   cpsie i                         \n"  /* globally enable interrupts  */
        "   cpsie f                         \n"
        "   dsb                             \n"
        "   isb                             \n"
        "   svc  0                          \n"  /* call vPortSVCHandler        */
        "   nop                             \n"
    );
}

/* =========================================================================
 * prvEnableVFP
 * Enable the Cortex-M FPU (set CP10/CP11 to full access).
 * ========================================================================= */
static void prvEnableVFP( void )
{
    __asm volatile (
        "   ldr.w r0, =0xE000ED88           \n"  /* CPACR                      */
        "   ldr   r1, [r0]                  \n"
        "   orr   r1, r1, #( 0xF << 20 )   \n"  /* CP10/CP11 full access       */
        "   str   r1, [r0]                  \n"
        "   bx    lr                        \n"
    );
}

/* =========================================================================
 * xPortPendSVHandler  (aliased as PendSV_Handler in FreeRTOSConfig.h)
 * Performs the context switch.  Saves R4-R11 + EXC_RETURN (+ S16-S31 if
 * the task was using the FPU) then calls vTaskSwitchContext, then restores.
 * ========================================================================= */
void xPortPendSVHandler( void ) __attribute__( ( naked ) );
void xPortPendSVHandler( void )
{
    __asm volatile (
        "   mrs   r0, psp                   \n"  /* r0 = current PSP           */
        "   isb                             \n"
        "                                   \n"
        "   ldr   r3, pxCurrentTCBConst     \n"  /* r3 = &pxCurrentTCB         */
        "   ldr   r2, [r3]                  \n"  /* r2 = pxCurrentTCB          */
        "                                   \n"
        "   tst   r14, #0x10               \n"  /* EXC_RETURN[4]=0 → FPU used */
        "   it    eq                        \n"
        "   vstmdbeq r0!, {s16-s31}        \n"  /* save high FP regs if needed */
        "                                   \n"
        "   stmdb r0!, {r4-r11, r14}       \n"  /* save R4-R11 + EXC_RETURN   */
        "   str   r0, [r2]                  \n"  /* update TCB top-of-stack     */
        "                                   \n"
        "   stmdb sp!, {r0, r3}            \n"  /* preserve r0, r3 across call */
        "   mov   r0, %0                    \n"
        "   msr   basepri, r0              \n"  /* mask interrupts             */
        "   dsb                             \n"
        "   isb                             \n"
        "   bl    vTaskSwitchContext        \n"  /* select new task             */
        "   mov   r0, #0                    \n"
        "   msr   basepri, r0              \n"  /* re-enable interrupts        */
        "   ldmia sp!, {r0, r3}            \n"  /* restore                     */
        "                                   \n"
        "   ldr   r1, [r3]                  \n"  /* r1 = new pxCurrentTCB       */
        "   ldr   r0, [r1]                  \n"  /* r0 = new top-of-stack       */
        "                                   \n"
        "   ldmia r0!, {r4-r11, r14}       \n"  /* restore R4-R11 + EXC_RETURN */
        "                                   \n"
        "   tst   r14, #0x10               \n"
        "   it    eq                        \n"
        "   vldmiaeq r0!, {s16-s31}        \n"  /* restore high FP if needed   */
        "                                   \n"
        "   msr   psp, r0                   \n"  /* update PSP to new task      */
        "   isb                             \n"
        "   bx    r14                       \n"  /* return to new task          */
        "                                   \n"
        "   .align 4                        \n"
        "pxCurrentTCBConst: .word pxCurrentTCB \n"
        :: "i" ( configMAX_SYSCALL_INTERRUPT_PRIORITY )
    );
}

/* =========================================================================
 * xPortSysTickHandler
 * Called by SysTick (configured by xPortStartScheduler).
 * Defined as SysTick_Handler via a weak alias so it wins over the startup stub.
 * ========================================================================= */
void xPortSysTickHandler( void );
void xPortSysTickHandler( void )
{
    /* Increment tick count, request context switch if needed. */
    portDISABLE_INTERRUPTS();
    {
        if( xTaskIncrementTick() != pdFALSE )
        {
            portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT;
        }
    }
    portENABLE_INTERRUPTS();
}

/* SysTick_Handler is provided by cmsis_os2.c when CMSIS_RTOS_V2 is used.
 * cmsis_os2.c calls xPortSysTickHandler() from within its SysTick_Handler.
 * Do NOT define a duplicate alias here. */

/* =========================================================================
 * vPortSetupTimerInterrupt
 * Configure SysTick at configTICK_RATE_HZ.  Called from xPortStartScheduler.
 * ========================================================================= */
__attribute__( ( weak ) ) void vPortSetupTimerInterrupt( void )
{
    portNVIC_SYSTICK_CTRL_REG = 0UL;
    portNVIC_SYSTICK_CURRENT_VALUE_REG = 0UL;

    /* Set PendSV and SysTick to lowest interrupt priority. */
    portNVIC_SHPR3_REG |= portNVIC_PENDSV_PRI;
    portNVIC_SHPR3_REG |= portNVIC_SYSTICK_PRI;

    /* Configure SysTick reload value for 1-ms tick (core clock source). */
    portNVIC_SYSTICK_LOAD_REG = ( configCPU_CLOCK_HZ / configTICK_RATE_HZ ) - 1UL;
    portNVIC_SYSTICK_CTRL_REG = portNVIC_SYSTICK_CLK_BIT |
                                 portNVIC_SYSTICK_INT_BIT |
                                 portNVIC_SYSTICK_ENABLE_BIT;
}

/* =========================================================================
 * xPortStartScheduler
 * ========================================================================= */
BaseType_t xPortStartScheduler( void )
{
    /* Enable lazy FPU context stacking (hardware-controlled). */
    prvEnableVFP();
    portFPCCR |= portASPEN_AND_LSPEN_BITS;

    /* Configure SysTick. */
    vPortSetupTimerInterrupt();

    /* Reset the critical-nesting count for the first task. */
    uxCriticalNesting = 0;

    /* Start the first task via SVC. */
    prvStartFirstTask();

    /* Should never reach here. */
    return pdFALSE;
}

/* =========================================================================
 * vPortEndScheduler
 * Not implemented — embedded targets don't stop the scheduler.
 * ========================================================================= */
void vPortEndScheduler( void )
{
    /* Not supported. */
    configASSERT( pdFALSE );
}

/* =========================================================================
 * Critical section nesting
 * ========================================================================= */
void vPortEnterCritical( void )
{
    portDISABLE_INTERRUPTS();
    uxCriticalNesting++;
    __asm volatile ( "DSB" ::: "memory" );
    __asm volatile ( "ISB" );
}

void vPortExitCritical( void )
{
    configASSERT( uxCriticalNesting > 0U );
    uxCriticalNesting--;
    if( uxCriticalNesting == 0U )
    {
        portENABLE_INTERRUPTS();
    }
}

/* =========================================================================
 * vPortValidateInterruptPriority
 * Called by portASSERT_IF_INTERRUPT_PRIORITY_INVALID() when configASSERT
 * is defined.  Verifies the calling ISR has a safe priority.
 * ========================================================================= */
#ifdef configASSERT
void vPortValidateInterruptPriority( void )
{
    uint32_t ulCurrentInterrupt;
    uint8_t ucCurrentPriority;

    __asm volatile ( "MRS %0, ipsr" : "=r" ( ulCurrentInterrupt ) :: "memory" );

    if( ulCurrentInterrupt != 0 )
    {
        /* Read the NVIC priority for this interrupt. */
        ucCurrentPriority = ( ( uint8_t * ) 0xE000E3F0UL )[ ulCurrentInterrupt - 16 ];
        configASSERT( ucCurrentPriority >=
                      ( uint8_t ) configMAX_SYSCALL_INTERRUPT_PRIORITY );
    }
}
#endif /* configASSERT */
