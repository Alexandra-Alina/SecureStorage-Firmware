/**
 * http_client_task.c — stub for NO_SYS bare-metal mode.
 *
 * HTTP tests are now run inline from main.c using the raw-TCP http_client.c.
 * This file is kept in the build to avoid Makefile churn; HttpClientTask()
 * is never called when NO_SYS=1.
 */

#include <stdio.h>
#include "http_client.h"
#include "http_client_config.h"
#include "main.h"

extern struct netif gnetif;

/* Not called in NO_SYS mode — present only for linker symbol completeness. */
void HttpClientTask(void *argument)
{
    (void)argument;
    /* Nothing — HTTP tests run inline in main.c. */
}
