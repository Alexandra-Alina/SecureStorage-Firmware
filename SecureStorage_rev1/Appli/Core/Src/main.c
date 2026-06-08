/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body — NO_SYS bare-metal mode
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "lwip.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "ethernetif.h"
#include "lwip/timeouts.h"
#include "cert_strategies.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

COM_InitTypeDef BspCOMInit;
__IO uint32_t BspButtonState = BUTTON_RELEASED;

CRYP_HandleTypeDef hcryp;
__ALIGN_BEGIN static const uint32_t pKeyCRYP[4] __ALIGN_END = {
                            0x00000000,0x00000000,0x00000000,0x00000000};

/* USER CODE BEGIN PV */
/* Seconds to wait between consecutive benchmark runs (keeps network alive). */
#define BENCHMARK_LOOP_PAUSE_S  5U
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
static void MX_GPIO_Init(void);
static void MX_CRYP_Init(void);

/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* Configure MPU before enabling D-Cache.
   *
   * Region 2 — NONCACHE 1 KB (0x24071C00): ETH DMA descriptor tables plus
   *   cert-strategy noncacheable buffers.  TEX=0, C=0, B=1 = "Device memory":
   *   strictly ordered, non-speculative, write-buffer bypassed — guarantees
   *   that descriptor OWN-bit writes are immediately visible to the DMA.
   *
   * Region 3 — LwIP heap 16 KB (0x24040000): LWIP_RAM_HEAP_POINTER region.
   *   pbuf payloads for outgoing packets (ARP, ICMP, TCP) are allocated here.
   *   HAL_ETH_Transmit has NO SCB_CleanDCache call, so if the heap is
   *   cacheable the DMA reads stale zeros and TX is silently broken.
   *   TEX=1, C=0, B=0 = Normal non-cacheable fixes this.
   */
  {
      HAL_MPU_Disable();
      MPU_Region_InitTypeDef mpu = {0};

      /* Region 2: NONCACHE section — Device memory */
      mpu.Enable           = MPU_REGION_ENABLE;
      mpu.Number           = MPU_REGION_NUMBER2;
      mpu.BaseAddress      = 0x24071C00U;
      mpu.Size             = MPU_REGION_SIZE_1KB;
      mpu.SubRegionDisable = 0x00U;
      mpu.TypeExtField     = MPU_TEX_LEVEL0;          /* TEX=0              */
      mpu.AccessPermission = MPU_REGION_FULL_ACCESS;
      mpu.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
      mpu.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
      mpu.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE; /* C=0                */
      mpu.IsBufferable     = MPU_ACCESS_BUFFERABLE;    /* B=1 → Device mem   */
      HAL_MPU_ConfigRegion(&mpu);

      /* Region 3: LwIP heap — Normal non-cacheable */
      mpu.Number           = MPU_REGION_NUMBER3;
      mpu.BaseAddress      = 0x24040000U;
      mpu.Size             = MPU_REGION_SIZE_16KB;
      mpu.TypeExtField     = MPU_TEX_LEVEL1;           /* TEX=1              */
      mpu.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
      mpu.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE; /* C=0                */
      mpu.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE; /* B=0 → Normal NC   */
      HAL_MPU_ConfigRegion(&mpu);

      HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
  }
  /* USER CODE END 1 */

  /* Enable I-Cache */
  SCB_EnableICache();
  /* Enable D-Cache */
  SCB_EnableDCache();

  /* MCU Configuration */
  SystemCoreClockUpdate();
  HAL_Init();

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  /* Initialize GPIO first (needed by COM1 BSP) */
  MX_GPIO_Init();

  BSP_LED_Init(LED_GREEN);
  BSP_LED_Init(LED_YELLOW);
  BSP_LED_Init(LED_RED);

  BspCOMInit.BaudRate   = 115200;
  BspCOMInit.WordLength = COM_WORDLENGTH_8B;
  BspCOMInit.StopBits   = COM_STOPBITS_1;
  BspCOMInit.Parity     = COM_PARITY_NONE;
  BspCOMInit.HwFlowCtl  = COM_HWCONTROL_NONE;
  if (BSP_COM_Init(COM1, &BspCOMInit) != BSP_ERROR_NONE)
  {
    Error_Handler();
  }

  printf("Welcome to STM32 world !\r\nApplication project is running...\r\n");
  BSP_LED_On(LED_GREEN);

  printf("[main] Initializing CRYP...\r\n");
  MX_CRYP_Init();
  printf("[main] Peripherals OK. Starting benchmark...\r\n");

  /* USER CODE BEGIN 2 */

  /* ----------------------------------------------------------------
   * LwIP init (NO_SYS=1 polling mode).
   * Static IP — no DHCP, avoids any DHCP-related RX dependency.
   * Wait 500 ms for LAN8742 PHY PLL to stabilise on PD6.
   * ---------------------------------------------------------------- */
  printf("[main] Waiting 500 ms for LAN8742 PHY REFCLK...\r\n");
  HAL_Delay(500);

  printf("[main] Initialising LwIP (NO_SYS polling mode)...\r\n");
  MX_LWIP_Init();
  printf("[main] LwIP init done.\r\n");

  extern struct netif gnetif;
  extern volatile int32_t g_eth_hal_init_status;
  extern volatile int32_t g_eth_hal_start_status;
  printf("[main] HAL_ETH_Init=%ld  HAL_ETH_Start=%ld\r\n",
         (long)g_eth_hal_init_status, (long)g_eth_hal_start_status);

  /* ----------------------------------------------------------------
   * Static IP configuration — skip DHCP entirely.
   * Assign address before waiting for link so ARP gratuitous announcements
   * can go out as soon as the link comes up.
   * ---------------------------------------------------------------- */
  {
    ip4_addr_t ip, nm, gw_addr;
    IP4_ADDR(&ip,      192, 168, 0, 100);
    IP4_ADDR(&nm,      255, 255, 255, 0);
    IP4_ADDR(&gw_addr, 192, 168, 0,   1);
    netif_set_addr(&gnetif, &ip, &nm, &gw_addr);
    netif_set_up(&gnetif);
    printf("[main] Static IP: 192.168.0.100  GW: 192.168.0.1\r\n");
  }

  /* ----------------------------------------------------------------
   * Polling loop: wait for PHY link UP (up to 10 s).
   * ---------------------------------------------------------------- */
  uint32_t t_start    = HAL_GetTick();
  uint32_t t_link_chk = 0U;
  uint32_t t_diag     = 0U;

  printf("[main] Polling for PHY link UP...\r\n");

  while (!netif_is_link_up(&gnetif) && (HAL_GetTick() - t_start < 10000U))
  {
    uint32_t now = HAL_GetTick();

    ethernetif_poll(&gnetif);
    sys_check_timeouts();

    if (now - t_link_chk >= 200U)
    {
      t_link_chk = now;
      ethernet_link_check(&gnetif);
    }

    if (now - t_diag >= 1000U)
    {
      t_diag = now;
      extern volatile uint32_t g_eth_rx_count;
      extern volatile int32_t g_eth_hal_init_status;
      extern volatile int32_t g_eth_hal_start_status;
      extern ETH_HandleTypeDef heth;
      printf("[main] t=%lus  link=%d  up=%d  rx=%lu  hal_init=%ld  hal_start=%ld  DMACSR=0x%08lX\r\n",
             (now - t_start) / 1000U,
             (int)netif_is_link_up(&gnetif),
             (int)netif_is_up(&gnetif),
             (unsigned long)g_eth_rx_count,
             (long)g_eth_hal_init_status,
             (long)g_eth_hal_start_status,
             (unsigned long)heth.Instance->DMACSR);
    }
  }

  if (netif_is_link_up(&gnetif))
  {
    printf("[main] Link UP - IP: 192.168.0.100\r\n");
    extern ETH_HandleTypeDef heth;
    extern volatile uint32_t g_rx_alloc_cnt;

    /* MAC / DMA register snapshot — key bits:
     *   MACCR  bit0=RE (Rx Enable) bit1=TE (Tx Enable)
     *   DMACRCR bit0=SR (Rx DMA running) bits[14:1]=RBSZ (buf size>>1)
     *   MACPFR  bit0=PR (promiscuous) bit31=RA (receive all)
     *   DMACRDLAR = address of Rx descriptor ring (must be in NONCACHE) */
    printf("[ETH] MACCR=0x%08lX  DMACRCR=0x%08lX  MACPFR=0x%08lX\r\n",
           (unsigned long)heth.Instance->MACCR,
           (unsigned long)heth.Instance->DMACRCR,
           (unsigned long)heth.Instance->MACPFR);
    printf("[ETH] DMACRDLAR=0x%08lX  DMACRDTPR=0x%08lX  DMACRDRLR=0x%08lX\r\n",
           (unsigned long)heth.Instance->DMACRDLAR,
           (unsigned long)heth.Instance->DMACRDTPR,
           (unsigned long)heth.Instance->DMACRDRLR);
    printf("[ETH] DMACSR=0x%08lX  DMACIER=0x%08lX  alloc_cnt=%lu\r\n",
           (unsigned long)heth.Instance->DMACSR,
           (unsigned long)heth.Instance->DMACIER,
           (unsigned long)g_rx_alloc_cnt);

  }
  else
  {
    printf("[main] Link timeout - continuing with static IP (no link).\r\n");
  }

  BSP_LED_Off(LED_RED);

  /* USER CODE END 2 */

  /* ----------------------------------------------------------------
   * Benchmark loop — run S1-S8, POST telemetry, pause, repeat.
   * BENCHMARK_LOOP_PAUSE_S seconds between runs; network kept alive.
   * ---------------------------------------------------------------- */
  /* USER CODE BEGIN WHILE */
  uint32_t run_id = 0U;
  uint32_t t_led  = 0U;
  while (1)
  {
    run_id++;
    printf("\r\n[main] ========== Run #%lu ==========\r\n",
           (unsigned long)run_id);
    cert_strategies_run_all();
    BSP_LED_Toggle(LED_GREEN);

    /* Pause between runs — keep LwIP timers and link check alive */
    printf("[main] Pausing %lu s before run #%lu...\r\n",
           (unsigned long)BENCHMARK_LOOP_PAUSE_S,
           (unsigned long)(run_id + 1U));
    uint32_t t_pause = HAL_GetTick();
    while (HAL_GetTick() - t_pause < BENCHMARK_LOOP_PAUSE_S * 1000U)
    {
      ethernetif_poll(&gnetif);
      sys_check_timeouts();

      uint32_t now = HAL_GetTick();
      if (now - t_link_chk >= 200U)
      {
        t_link_chk = now;
        ethernet_link_check(&gnetif);
      }
      if (now - t_led >= 500U)
      {
        t_led = now;
        BSP_LED_Toggle(LED_YELLOW);
      }
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief CRYP Initialization Function
  */
static void MX_CRYP_Init(void)
{
  /* USER CODE BEGIN CRYP_Init 0 */
  /* USER CODE END CRYP_Init 0 */
  /* USER CODE BEGIN CRYP_Init 1 */
  /* USER CODE END CRYP_Init 1 */
  hcryp.Instance = CRYP;
  hcryp.Init.DataType = CRYP_DATATYPE_32B;
  hcryp.Init.KeySize = CRYP_KEYSIZE_128B;
  hcryp.Init.pKey = (uint32_t *)pKeyCRYP;
  hcryp.Init.Algorithm = CRYP_AES_ECB;
  hcryp.Init.DataWidthUnit = CRYP_DATAWIDTHUNIT_WORD;
  hcryp.Init.HeaderWidthUnit = CRYP_HEADERWIDTHUNIT_WORD;
  hcryp.Init.KeyIVConfigSkip = CRYP_KEYIVCONFIG_ALWAYS;
  hcryp.Init.KeyMode = CRYP_KEYMODE_NORMAL;
  if (HAL_CRYP_Init(&hcryp) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CRYP_Init 2 */
  /* USER CODE END CRYP_Init 2 */
}

/**
  * @brief GPIO Initialization Function
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */
  /* USER CODE END MX_GPIO_Init_1 */

  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  HAL_TIM period elapsed callback (TIM1 = HAL timebase).
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */
  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM1) {
    HAL_IncTick();
    /* Heartbeat: slow 2 s blink so board appears alive between strategies */
    static uint32_t led_cnt = 0;
    if (++led_cnt >= 2000U) {
      led_cnt = 0;
      BSP_LED_Toggle(LED_YELLOW);
    }
  }
  /* USER CODE BEGIN Callback 1 */
  /* USER CODE END Callback 1 */
}

/**
  * @brief  Error handler — disables interrupts and halts.
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
