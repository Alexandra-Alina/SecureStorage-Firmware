/*
 * FreeRTOS Kernel V10.6.2
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 * SPDX-License-Identifier: MIT
 *
 * GCC / ARM Cortex-M4F (also used for Cortex-M7) port.
 * Compatible with STM32H7S3 (Cortex-M7, ARMv7-M with FPUv5-D16).
 */

#ifndef PORTMACRO_H
#define PORTMACRO_H

#ifdef __cplusplus
extern "C" {
#endif

/*-----------------------------------------------------------
 * Port specific definitions.
 *-----------------------------------------------------------*/

/* Type definitions. */
#define portCHAR        char
#define portFLOAT       float
#define portDOUBLE      double
#define portLONG        long
#define portSHORT       short
#define portSTACK_TYPE  uint32_t
#define portBASE_TYPE   long

typedef portSTACK_TYPE  StackType_t;
typedef long            BaseType_t;
typedef unsigned long   UBaseType_t;

#if ( configUSE_16_BIT_TICKS == 1 )
    typedef uint16_t    TickType_t;
    #define portMAX_DELAY   ( TickType_t ) 0xffffU
#else
    typedef uint32_t    TickType_t;
    #define portMAX_DELAY   ( TickType_t ) 0xffffffffUL
    /* 32-bit tick on 32-bit arch — reads are atomic. */
    #define portTICK_TYPE_IS_ATOMIC  1
#endif

/*-----------------------------------------------------------*/

/* Architecture specifics. */
#define portSTACK_GROWTH      ( -1 )
#define portTICK_PERIOD_MS    ( ( TickType_t ) 1000 / configTICK_RATE_HZ )
#define portBYTE_ALIGNMENT    8
#define portNOP()             __asm volatile ( "NOP" )

#ifndef portFORCE_INLINE
    #define portFORCE_INLINE  inline __attribute__( ( always_inline ) )
#endif
#define portINLINE  __inline
#define portWEAK_SYMBOL  __attribute__( ( weak ) )

/*-----------------------------------------------------------*/

/* Scheduler utilities — request context switch via PendSV. */
#define portNVIC_INT_CTRL_REG     ( *( ( volatile uint32_t * ) 0xe000ed04UL ) )
#define portNVIC_PENDSVSET_BIT    ( 1UL << 28UL )

#define portYIELD()                                         \
{                                                           \
    portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT;         \
    __asm volatile ( "DSB" ::: "memory" );                  \
    __asm volatile ( "ISB" );                               \
}

#define portEND_SWITCHING_ISR( xSwitchRequired ) \
    do { if( ( xSwitchRequired ) != pdFALSE ) portYIELD(); } while( 0 )
#define portYIELD_FROM_ISR( x )   portEND_SWITCHING_ISR( x )

/*-----------------------------------------------------------*/

/* Critical section management — uses BASEPRI to mask interrupts. */
extern void vPortEnterCritical( void );
extern void vPortExitCritical( void );

static portFORCE_INLINE void vPortSetBASEPRI( uint32_t ulBASEPRI )
{
    __asm volatile ( "MSR basepri, %0" :: "r" ( ulBASEPRI ) : "memory" );
}

static portFORCE_INLINE uint32_t ulPortRaiseBASEPRI( void )
{
    uint32_t ulReturn;
    uint32_t ulNewBASEPRI = configMAX_SYSCALL_INTERRUPT_PRIORITY;
    __asm volatile (
        "MRS %0, basepri    \n"
        "MSR basepri, %1    \n"
        "ISB                \n"
        "DSB                \n"
        : "=r" ( ulReturn ) : "r" ( ulNewBASEPRI ) : "memory"
    );
    return ulReturn;
}

#define portDISABLE_INTERRUPTS()                                       \
{                                                                      \
    uint32_t ulNewBASEPRI = configMAX_SYSCALL_INTERRUPT_PRIORITY;      \
    __asm volatile ( "MSR basepri, %0\n ISB\n DSB\n"                   \
                     :: "r" ( ulNewBASEPRI ) : "memory" );             \
}

#define portENABLE_INTERRUPTS()             vPortSetBASEPRI( 0 )
#define portENTER_CRITICAL()                vPortEnterCritical()
#define portEXIT_CRITICAL()                 vPortExitCritical()
#define portSET_INTERRUPT_MASK_FROM_ISR()   ulPortRaiseBASEPRI()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR( x ) vPortSetBASEPRI( x )

/*-----------------------------------------------------------*/

/* Return whether currently inside an ISR (read IPSR). */
static portFORCE_INLINE BaseType_t xPortIsInsideInterrupt( void )
{
    uint32_t ulCurrentInterrupt;
    __asm volatile ( "MRS %0, ipsr" : "=r" ( ulCurrentInterrupt ) :: "memory" );
    return ( ulCurrentInterrupt != 0 ) ? pdTRUE : pdFALSE;
}

/*-----------------------------------------------------------*/

/* Task function macros. */
#define portTASK_FUNCTION_PROTO( vFunction, pvParameters ) \
    void vFunction( void * pvParameters )
#define portTASK_FUNCTION( vFunction, pvParameters ) \
    void vFunction( void * pvParameters )

/*-----------------------------------------------------------*/

#ifdef configASSERT
    void vPortValidateInterruptPriority( void );
    #define portASSERT_IF_INTERRUPT_PRIORITY_INVALID() \
        vPortValidateInterruptPriority()
#endif

#ifdef __cplusplus
}
#endif

#endif /* PORTMACRO_H */
