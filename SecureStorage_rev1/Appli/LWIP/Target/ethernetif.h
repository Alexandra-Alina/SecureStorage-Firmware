/* USER CODE BEGIN Header */
/**
 ******************************************************************************
  * File Name          : ethernetif.h
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

#ifndef __ETHERNETIF_H
#define __ETHERNETIF_H
#include "lwip/err.h"
#include "lwip/netif.h"

/* Within 'USER CODE' section, code will be kept by default at each generation */
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* Exported functions ------------------------------------------------------- */
err_t ethernetif_init(struct netif *netif);

/** Poll for received Ethernet frames and feed them into the LwIP stack.
 *  Call from the main polling loop as often as possible. */
void ethernetif_poll(struct netif *netif);

/** Poll the LAN8742 PHY and update netif link/up state.
 *  Call every ~100 ms from the main polling loop. */
void ethernet_link_check(struct netif *netif);

u32_t sys_now(void);

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
#endif /* __ETHERNETIF_H */
