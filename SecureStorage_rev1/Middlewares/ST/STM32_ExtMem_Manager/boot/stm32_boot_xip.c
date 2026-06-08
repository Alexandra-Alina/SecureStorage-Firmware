/**
  ******************************************************************************
  * @file    stm32_boot_xip.c
  * @author  MCD Application Team
  * @brief   This file manages booting in execute-in-place (XIP) mode.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2022 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "stm32_boot_xip.h"
#include "stm32_extmem_conf.h"
#include "secure_boot.h"
#include "boot_bhk.h"
#include "stm32h7rsxx_nucleo.h"
#include <string.h>
#include <stdio.h>

/** @addtogroup BOOT
  * @{
  */

/** @addtogroup BOOT_XIP
  * @{
  */

/* Private typedefs ----------------------------------------------------------*/
/* Private defines -----------------------------------------------------------*/

/* ── BHK: load wrap key into SAES hardware registers ─────────────────────
 * Key source : BHK_WRAP_KEY in Boot flash (protected by RDP Level 1)
 * Key dest   : SAES hardware key registers (write-only, retained after jump)
 * Appli      : uses SAES without ever seeing the key in plaintext
 * ──────────────────────────────────────────────────────────────────────── */
static void bhk_load_into_saes(void)
{
    /* Enable SAES clock — no peripheral reset (reset triggers DHUK load → BUSY) */
    SET_BIT(RCC->AHB3ENR, RCC_AHB3ENR_SAESEN);
    __DSB();
    __ISB();

    /* Disable SAES (EN=0) before configuring — clears any previous operation */
    CLEAR_BIT(SAES->CR, SAES_CR_EN);

    /* Configure: AES-256, ECB, no byte-swap, normal key from software registers */
    SAES->CR = (1U << SAES_CR_KEYSIZE_Pos);   /* KEYSIZE=1 (256-bit), CHMOD=00 (ECB), DATATYPE=00 */

    /* Write 256-bit BHK into SAES key registers (K7=MSW ... K0=LSW) */
    const uint32_t *k = (const uint32_t *)(uintptr_t)BHK_WRAP_KEY;
    SAES->KEYR7 = __REV(k[0]);
    SAES->KEYR6 = __REV(k[1]);
    SAES->KEYR5 = __REV(k[2]);
    SAES->KEYR4 = __REV(k[3]);
    SAES->KEYR3 = __REV(k[4]);
    SAES->KEYR2 = __REV(k[5]);
    SAES->KEYR1 = __REV(k[6]);
    SAES->KEYR0 = __REV(k[7]);

    /* Enable SAES to trigger key expansion (AES-256 key schedule).
     * CCF is set when expansion is complete. Key stays valid after EN=0. */
    SET_BIT(SAES->CR, SAES_CR_EN);
    uint32_t t2 = HAL_GetTick();
    while (!READ_BIT(SAES->SR, SAES_SR_CCF))
    {
        if (HAL_GetTick() - t2 > 200U)
        {
            printf("[boot] BHK: key expansion timeout.\r\n");
            CLEAR_BIT(SAES->CR, SAES_CR_EN);
            return;
        }
    }
    /* Clear CCF, disable SAES — key remains in registers, KEYVALID=1 */
    SET_BIT(SAES->ICR, SAES_ICR_CCF);
    CLEAR_BIT(SAES->CR, SAES_CR_EN);

    printf("[boot] BHK loaded + key expansion OK — SAES ready for Appli.\r\n");
}

/* Offset of the image from the boot memory base */
#ifndef EXTMEM_XIP_IMAGE_OFFSET
#define EXTMEM_XIP_IMAGE_OFFSET 0
#endif /* EXTMEM_XIP_IMAGE_OFFSET */

/* Offset of the vector table from the start of the image */
#ifndef EXTMEM_HEADER_OFFSET
#define EXTMEM_HEADER_OFFSET 0
#endif /* EXTMEM_HEADER_OFFSET */

/* Private macros ------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
BOOTStatus_TypeDef JumpToApplication(void);
BOOTStatus_TypeDef MapMemory(void);
BOOTStatus_TypeDef GetBaseAddress(uint32_t MemIndex, uint32_t *BaseAddress);

/**
  *  @addtogroup BOOT_XIP_Exported_Functions Boot XIP exported functions
  * @{
  */

/**
  * @brief Boots the application by mapping the memory and jumping to the application.
  * @retval BOOTStatus_TypeDef Status of the operation.
  */
BOOTStatus_TypeDef BOOT_Application(void)
{
  BOOTStatus_TypeDef retr;

  /* Mount the memory — NOR flash memory-mapped at 0x70000000 after this */
  retr = MapMemory();
  if (BOOT_OK == retr)
  {
    /* Secure boot: verify Appli HMAC-SHA256 now that NOR is mapped */
    printf("[boot] Verifying firmware integrity (HMAC-SHA256)...\r\n");
    uint32_t t0 = HAL_GetTick();
    if (secure_boot_verify() != SECURE_BOOT_OK)
    {
      printf("[boot] *** FIRMWARE VERIFICATION FAILED — halting! ***\r\n");
      BSP_LED_Off(LED_GREEN);
      BSP_LED_Off(LED_YELLOW);
      while (1)
      {
        BSP_LED_Toggle(LED_RED);
        HAL_Delay(200);
      }
    }
    uint32_t elapsed_ms = HAL_GetTick() - t0;
    printf("[boot] Firmware OK  (HMAC verified in %lu ms)\r\n", elapsed_ms);

    /* BHK/SAES: requires OTP provisioning — skipped on eval board */

    /* Jump on the application */
    retr = JumpToApplication();
  }
  return retr;
}

/**
  * @}
  */

/**
  *  @defgroup BOOT_XIP_Private_Functions Boot XIP private functions
  * @{
  */

/**
  * @brief  Maps the external memory.
  * @retval BOOTStatus_TypeDef Status of the operation.
  */
BOOTStatus_TypeDef MapMemory(void)
{
  BOOTStatus_TypeDef retr = BOOT_OK;

  /* Map all the memory */
  for (uint8_t index = 0; index < (sizeof(extmem_list_config) / sizeof(EXTMEM_DefinitionTypeDef)); index++)
  {
    switch (EXTMEM_MemoryMappedMode(index, EXTMEM_ENABLE))
    {
      case EXTMEM_ERROR_NOTSUPPORTED :
        if (EXTMEM_MEMORY_BOOTXIP ==  index)
        {
          retr = BOOT_ERROR_INCOMPATIBLEMEMORY;
        }
        else
        {
          /* We consider the memory will be not used any more */
          EXTMEM_DeInit(index);
        }
      case EXTMEM_OK:
        break;
      default :
        retr = BOOT_ERROR_MAPPEDMODEFAIL;
        break;
    }
  }
  return retr;
}

/**
  * @brief  Jumps to the application using its vector table.
  * @retval BOOTStatus_TypeDef Status of the operation.
  */
BOOTStatus_TypeDef JumpToApplication(void)
{
  uint32_t primask_bit;
  typedef  void (*pFunction)(void);
  static pFunction JumpToApp;
  uint32_t Application_vector;

  if (EXTMEM_OK != EXTMEM_GetMapAddress(EXTMEM_MEMORY_BOOTXIP, &Application_vector))
  {
    return BOOT_ERROR_INCOMPATIBLEMEMORY;
  }

  /* Suspend SysTick */
  HAL_SuspendTick();

#if defined(__ICACHE_PRESENT) && (__ICACHE_PRESENT == 1U)
  /* if I-Cache is enabled, disable I-Cache-----------------------------------*/
  if (SCB->CCR & SCB_CCR_IC_Msk)
  {
    SCB_DisableICache();
  }
#endif /* __ICACHE_PRESENT */

#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  /* if D-Cache is enabled, disable D-Cache-----------------------------------*/
  if (SCB->CCR & SCB_CCR_DC_Msk)
  {
    SCB_DisableDCache();
  }
#endif /* __DCACHE_PRESENT */

  /* Initialize user application's Stack Pointer & Jump to user application  */
  primask_bit = __get_PRIMASK();
  __disable_irq();

  /* Apply offsets for image location and vector table offset */
  Application_vector += EXTMEM_XIP_IMAGE_OFFSET + EXTMEM_HEADER_OFFSET;

  SCB->VTOR = (uint32_t)Application_vector;
  JumpToApp = (pFunction)(*(__IO uint32_t *)(Application_vector + 4u));

#if (defined(__ARM_ARCH_8M_MAIN__ ) && (__ARM_ARCH_8M_MAIN__ == 1))
  /* on ARM v8m, set MSPLIM before setting MSP to avoid unwanted stack overflow faults */
  __set_MSPLIM(0x00000000);
#elif (defined(__ARM_ARCH_8_1M_MAIN__ ) && (__ARM_ARCH_8_1M_MAIN__ == 1))
  /* on ARM v8m, set MSPLIM before setting MSP to avoid unwanted stack overflow faults */
  __set_MSPLIM(0x00000000);
#elif (defined(__ARM_ARCH_8M_BASE__ ) && (__ARM_ARCH_8M_BASE__ == 1))
  /* on ARM v8m, set MSPLIM before setting MSP to avoid unwanted stack overflow faults */
  __set_MSPLIM(0x00000000);
#endif  /* __ARM_ARCH_8M_MAIN__ or __ARM_ARCH_8_1M_MAIN__ or __ARM_ARCH_8M_BASE__ */

  __set_MSP(*(__IO uint32_t *) Application_vector);

  /* Re-enable the interrupts */
  __set_PRIMASK(primask_bit);

  JumpToApp();
  return BOOT_OK;
}

/**
  * @}
  */

/**
  * @}
 */

/**
  * @}
  */
