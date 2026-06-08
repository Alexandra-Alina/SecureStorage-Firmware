/* USER CODE BEGIN Header */
/**
 ******************************************************************************
  * File Name          : LWIP.c
  * Description        : This file provides initialization code for LWIP
  *                      middleWare.
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
#include "lwip.h"
#include "lwip/init.h"
#include "ethernetif.h"
#include "lwip/netif.h"
#include "netif/ethernet.h"
#include <string.h>

/* USER CODE BEGIN 0 */
/* Static IP only — no DHCP in NO_SYS bare-metal mode */
/* USER CODE END 0 */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define -----------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -----------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private function prototypes -----------------------------------------------*/
static void ethernet_link_status_updated(struct netif *netif);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Variables Initialization */
struct netif gnetif;
ip4_addr_t ipaddr;
ip4_addr_t netmask;
ip4_addr_t gw;

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/**
  * LwIP initialization function — NO_SYS=1 bare-metal mode.
  * Uses lwip_init() + netif_add() directly; no tcpip_thread, no RTOS.
  */
void MX_LWIP_Init(void)
{
  /* Initialize the LwIP stack (NO_SYS=1: single-threaded, polling) */
  lwip_init();

  /* IP addresses all-zero for DHCP (filled in later) */
  ipaddr.addr  = 0;
  netmask.addr = 0;
  gw.addr      = 0;

  /* Add the network interface.
   * With NO_SYS=1 the input function must be ethernet_input (not tcpip_input). */
  netif_add(&gnetif, &ipaddr, &netmask, &gw, NULL, &ethernetif_init, &ethernet_input);

  /* Register as the default network interface */
  netif_set_default(&gnetif);

  /* Set the link callback (runs in main context — no core-lock hazard with NO_SYS) */
  netif_set_link_callback(&gnetif, ethernet_link_status_updated);

  /* NOTE: Do NOT bring the interface up here.
   * ethernet_link_check() (called from the main polling loop) will call
   * netif_set_up() + netif_set_link_up() once the LAN8742 PHY reports link UP.
   * Starting DHCP before the link is up would fail silently (ERR_ARG in
   * LwIP 2.2.1 dhcp.c line 817). */
}

/**
  * @brief  Notify about link status changes (runs in main-loop context with NO_SYS=1).
  */
static void ethernet_link_status_updated(struct netif *netif)
{
  if (netif_is_link_up(netif))
  {
/* USER CODE BEGIN 5 */
    printf("[lwip] link UP\r\n");
/* USER CODE END 5 */
  }
  else
  {
/* USER CODE BEGIN 6 */
    printf("[lwip] link DOWN\r\n");
/* USER CODE END 6 */
  }
}

/* USER CODE BEGIN 11 */

/* USER CODE END 11 */
