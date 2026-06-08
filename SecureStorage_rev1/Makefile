##############################################################################
# SecureStorage_rev1 — GCC build (arm-none-eabi from STM32CubeIDE 1.13.2)
##############################################################################

TOOLCHAIN := C:/ST/STM32CubeIDE_1.13.2/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.100.202509120712/tools/bin
CC  := $(TOOLCHAIN)/arm-none-eabi-gcc
AS  := $(TOOLCHAIN)/arm-none-eabi-gcc -x assembler-with-cpp
CP  := $(TOOLCHAIN)/arm-none-eabi-objcopy
SZ  := $(TOOLCHAIN)/arm-none-eabi-size

PROGRAMMER := C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe
EXT_LOADER := C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/ExternalLoader/MX25UW25645G_NUCLEO-H7S3L8.stldr

ROOT := .

##############################################################################
# Common flags
##############################################################################
CPU_FLAGS := -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
OPT       := -O0 -g3
WFLAGS    := -Wall -ffunction-sections -fdata-sections

COMMON_DEFS := \
    -DUSE_HAL_DRIVER \
    -DSTM32H7S3xx

COMMON_FLAGS := $(CPU_FLAGS) $(OPT) $(WFLAGS) $(COMMON_DEFS)

##############################################################################
# BOOT project
##############################################################################
BOOT_BUILD := build/Boot
BOOT_ELF   := $(BOOT_BUILD)/SecureStorage_rev1_Boot.elf
BOOT_LD    := gcc/stm32h7s3xx_flash.ld

BOOT_STARTUP := $(ROOT)/Drivers/CMSIS/Device/ST/STM32H7RSxx/Source/Templates/gcc/startup_stm32h7s3xx.s

BOOT_INC := \
    -I$(ROOT)/Boot/Core/Inc \
    -I$(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Inc \
    -I$(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Inc/Legacy \
    -I$(ROOT)/Middlewares/ST/STM32_ExtMem_Manager \
    -I$(ROOT)/Middlewares/ST/STM32_ExtMem_Manager/boot \
    -I$(ROOT)/Middlewares/ST/STM32_ExtMem_Manager/sal \
    -I$(ROOT)/Middlewares/ST/STM32_ExtMem_Manager/nor_sfdp \
    -I$(ROOT)/Middlewares/ST/STM32_ExtMem_Manager/psram \
    -I$(ROOT)/Middlewares/ST/STM32_ExtMem_Manager/sdcard \
    -I$(ROOT)/Middlewares/ST/STM32_ExtMem_Manager/user \
    -I$(ROOT)/Drivers/BSP/STM32H7RSxx_Nucleo \
    -I$(ROOT)/Drivers/CMSIS/Device/ST/STM32H7RSxx/Include \
    -I$(ROOT)/Drivers/CMSIS/Include

BOOT_SRCS := \
    $(ROOT)/Boot/Core/Src/secure_boot.c \
    $(ROOT)/Boot/Core/Src/main.c \
    $(ROOT)/Boot/Core/Src/extmem_manager.c \
    $(ROOT)/Boot/Core/Src/stm32h7rsxx_it.c \
    $(ROOT)/Boot/Core/Src/stm32h7rsxx_hal_msp.c \
    $(ROOT)/Boot/Core/Src/stm32h7rsxx_hal_timebase_tim.c \
    $(ROOT)/Boot/Core/Src/system_stm32h7rsxx.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_tim.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_tim_ex.c \
    $(ROOT)/Drivers/BSP/STM32H7RSxx_Nucleo/stm32h7rsxx_nucleo.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_cortex.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_rcc.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_rcc_ex.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_flash.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_flash_ex.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_gpio.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_dma.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_dma_ex.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_pwr.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_pwr_ex.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_exti.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_xspi.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_uart.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_uart_ex.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_usart.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_usart_ex.c \
    $(ROOT)/Middlewares/ST/STM32_ExtMem_Manager/stm32_extmem.c \
    $(ROOT)/Middlewares/ST/STM32_ExtMem_Manager/boot/stm32_boot_xip.c \
    $(ROOT)/Middlewares/ST/STM32_ExtMem_Manager/sal/stm32_sal_xspi.c \
    $(ROOT)/Middlewares/ST/STM32_ExtMem_Manager/sal/stm32_sal_sd.c \
    $(ROOT)/Middlewares/ST/STM32_ExtMem_Manager/nor_sfdp/stm32_sfdp_data.c \
    $(ROOT)/Middlewares/ST/STM32_ExtMem_Manager/nor_sfdp/stm32_sfdp_driver.c \
    $(ROOT)/Middlewares/ST/STM32_ExtMem_Manager/psram/stm32_psram_driver.c \
    $(ROOT)/Middlewares/ST/STM32_ExtMem_Manager/sdcard/stm32_sdcard_driver.c \
    $(ROOT)/Middlewares/ST/STM32_ExtMem_Manager/user/stm32_user_driver.c \
    $(ROOT)/gcc/syscalls.c \
    $(ROOT)/gcc/sysmem.c

BOOT_OBJS := $(addprefix $(BOOT_BUILD)/,$(addsuffix .o,$(basename $(notdir $(BOOT_SRCS)))))
BOOT_OBJS += $(BOOT_BUILD)/startup_stm32h7s3xx.o

BOOT_LFLAGS := \
    $(CPU_FLAGS) \
    -T$(BOOT_LD) \
    -Wl,-Map=$(BOOT_BUILD)/Boot.map,--cref \
    -Wl,--gc-sections \
    --specs=nano.specs

##############################################################################
# APPLI project
##############################################################################
APPLI_BUILD := build/Appli
APPLI_ELF   := $(APPLI_BUILD)/SecureStorage_rev1_Appli.elf
APPLI_BIN   := $(APPLI_BUILD)/SecureStorage_rev1_Appli.bin
APPLI_LD    := gcc/stm32h7rsxx_ROMxspi2.ld

APPLI_INC := \
    -I$(ROOT)/Appli/Core/Inc \
    -I$(ROOT)/Appli/LWIP/App \
    -I$(ROOT)/Appli/LWIP/Target \
    -I$(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Inc \
    -I$(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Inc/Legacy \
    -I$(ROOT)/Drivers/BSP/STM32H7RSxx_Nucleo \
    -I$(ROOT)/Drivers/BSP/Components/lan8742 \
    -I$(ROOT)/Drivers/CMSIS/Device/ST/STM32H7RSxx/Include \
    -I$(ROOT)/Drivers/CMSIS/Include \
    -I$(ROOT)/Drivers/CMSIS/RTOS2/Include \
    -I$(ROOT)/Middlewares/Third_Party/FreeRTOS/Source/include \
    -I$(ROOT)/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F \
    -I$(ROOT)/Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 \
    -I$(ROOT)/Middlewares/Third_Party/LwIP/src/include \
    -I$(ROOT)/Middlewares/Third_Party/LwIP/system \
    -I$(ROOT)/Middlewares/Third_Party/mbedtls/include

APPLI_SRCS := \
    $(ROOT)/Appli/Core/Src/main.c \
    $(ROOT)/Appli/Core/Src/freertos.c \
    $(ROOT)/Appli/Core/Src/stm32h7rsxx_it.c \
    $(ROOT)/Appli/Core/Src/stm32h7rsxx_hal_msp.c \
    $(ROOT)/Appli/Core/Src/stm32h7rsxx_hal_timebase_tim.c \
    $(ROOT)/Appli/Core/Src/system_stm32h7rsxx.c \
    $(ROOT)/Appli/Core/Src/cert_strategies.c \
    $(ROOT)/Appli/Core/Src/mem_measure.c \
    $(ROOT)/Appli/Core/Src/http_client.c \
    $(ROOT)/Appli/Core/Src/http_client_task.c \
    $(ROOT)/Appli/Core/Src/tls_client.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/aes.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/asn1parse.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/asn1write.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/base64.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/bignum.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/bignum_core.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/bignum_mod.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/bignum_mod_raw.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/block_cipher.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/cipher.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/cipher_wrap.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/constant_time.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/ctr_drbg.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/entropy.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/entropy_poll.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/error.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/gcm.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/md.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/mps_reader.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/mps_trace.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/oid.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/pem.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/pk.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/pk_wrap.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/pkparse.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/platform.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/platform_util.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/rsa.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/rsa_alt_helpers.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/sha1.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/sha256.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/sha512.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/ssl_ciphersuites.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/ssl_client.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/ssl_debug_helpers_generated.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/ssl_msg.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/ssl_tls.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/ssl_tls12_client.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/version.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/x509.c \
    $(ROOT)/Middlewares/Third_Party/mbedtls/library/x509_crt.c \
    $(ROOT)/Middlewares/Third_Party/FreeRTOS/Source/tasks.c \
    $(ROOT)/Middlewares/Third_Party/FreeRTOS/Source/list.c \
    $(ROOT)/Middlewares/Third_Party/FreeRTOS/Source/queue.c \
    $(ROOT)/Middlewares/Third_Party/FreeRTOS/Source/timers.c \
    $(ROOT)/Middlewares/Third_Party/FreeRTOS/Source/event_groups.c \
    $(ROOT)/Middlewares/Third_Party/FreeRTOS/Source/stream_buffer.c \
    $(ROOT)/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F/port.c \
    $(ROOT)/Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c \
    $(ROOT)/Drivers/BSP/STM32H7RSxx_Nucleo/stm32h7rsxx_nucleo.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_cortex.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_rcc.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_rcc_ex.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_flash.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_flash_ex.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_gpio.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_dma.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_dma_ex.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_pwr.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_pwr_ex.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_exti.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_uart.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_uart_ex.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_usart.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_usart_ex.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_cryp.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_cryp_ex.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_rng.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_rng_ex.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_tim.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_tim_ex.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_eth.c \
    $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal_eth_ex.c \
    $(ROOT)/Drivers/BSP/Components/lan8742/lan8742.c \
    $(ROOT)/Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/cmsis_os2.c \
    $(ROOT)/Appli/LWIP/App/lwip.c \
    $(ROOT)/Appli/LWIP/Target/ethernetif.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/init.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/def.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/dns.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/inet_chksum.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/ip.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/mem.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/memp.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/netif.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/pbuf.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/raw.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/stats.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/sys.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/tcp.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/tcp_in.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/tcp_out.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/timeouts.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/udp.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/altcp.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/altcp_alloc.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/altcp_tcp.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/ipv4/acd.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/ipv4/autoip.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/ipv4/dhcp.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/ipv4/etharp.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/ipv4/icmp.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/ipv4/igmp.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/ipv4/ip4.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/ipv4/ip4_addr.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/core/ipv4/ip4_frag.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/api/api_lib.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/api/api_msg.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/api/err.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/api/netbuf.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/api/netdb.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/api/netifapi.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/api/tcpip.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/src/netif/ethernet.c \
    $(ROOT)/Middlewares/Third_Party/LwIP/system/OS/sys_arch.c \
    $(ROOT)/gcc/syscalls.c \
    $(ROOT)/gcc/sysmem.c

APPLI_OBJS := $(addprefix $(APPLI_BUILD)/,$(addsuffix .o,$(basename $(notdir $(APPLI_SRCS)))))
APPLI_OBJS += $(APPLI_BUILD)/startup_stm32h7s3xx.o

APPLI_LFLAGS := \
    $(CPU_FLAGS) \
    -T$(APPLI_LD) \
    -Wl,-Map=$(APPLI_BUILD)/Appli.map,--cref \
    -Wl,--gc-sections \
    --specs=nano.specs \
    -u _printf_float

##############################################################################
# Targets
##############################################################################
##############################################################################
# Runtime / monitoring config
##############################################################################
UART_PORT := COM6
UART_BAUD := 115200
BOARD_IP  := 192.168.0.100
SERVER_PY := server/server.py

.PHONY: all boot appli flash flash-boot flash-appli clean sign secure-flash \
        server uart ping monitor run flash-run

all: boot appli

boot: $(BOOT_ELF)
	@$(SZ) $<

appli: $(APPLI_ELF)
	@$(SZ) $<

# Boot link
$(BOOT_ELF): $(BOOT_OBJS) $(BOOT_LD)
	@echo "[LD]  $@"
	@$(CC) $(BOOT_LFLAGS) $(BOOT_OBJS) -o $@

# Appli link
$(APPLI_ELF): $(APPLI_OBJS) $(APPLI_LD)
	@echo "[LD]  $@"
	@$(CC) $(APPLI_LFLAGS) $(APPLI_OBJS) -o $@

# Boot object compilation (pattern rules with vpath)
$(BOOT_BUILD)/%.o: $(ROOT)/Boot/Core/Src/%.c | $(BOOT_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(BOOT_INC) $< -o $@

$(BOOT_BUILD)/%.o: $(ROOT)/Drivers/BSP/STM32H7RSxx_Nucleo/%.c | $(BOOT_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(BOOT_INC) $< -o $@

$(BOOT_BUILD)/%.o: $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/%.c | $(BOOT_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(BOOT_INC) $< -o $@

$(BOOT_BUILD)/%.o: $(ROOT)/Middlewares/ST/STM32_ExtMem_Manager/%.c | $(BOOT_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(BOOT_INC) $< -o $@

$(BOOT_BUILD)/%.o: $(ROOT)/Middlewares/ST/STM32_ExtMem_Manager/boot/%.c | $(BOOT_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(BOOT_INC) $< -o $@

$(BOOT_BUILD)/%.o: $(ROOT)/Middlewares/ST/STM32_ExtMem_Manager/sal/%.c | $(BOOT_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(BOOT_INC) $< -o $@

$(BOOT_BUILD)/%.o: $(ROOT)/Middlewares/ST/STM32_ExtMem_Manager/nor_sfdp/%.c | $(BOOT_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(BOOT_INC) $< -o $@

$(BOOT_BUILD)/%.o: $(ROOT)/Middlewares/ST/STM32_ExtMem_Manager/psram/%.c | $(BOOT_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(BOOT_INC) $< -o $@

$(BOOT_BUILD)/%.o: $(ROOT)/Middlewares/ST/STM32_ExtMem_Manager/sdcard/%.c | $(BOOT_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(BOOT_INC) $< -o $@

$(BOOT_BUILD)/%.o: $(ROOT)/Middlewares/ST/STM32_ExtMem_Manager/user/%.c | $(BOOT_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(BOOT_INC) $< -o $@

$(BOOT_BUILD)/%.o: $(ROOT)/gcc/%.c | $(BOOT_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(BOOT_INC) $< -o $@

$(BOOT_BUILD)/startup_stm32h7s3xx.o: $(BOOT_STARTUP) | $(BOOT_BUILD)
	@echo "[AS]  $<"
	@$(AS) -c $(CPU_FLAGS) -g3 $< -o $@

$(BOOT_BUILD):
	@mkdir -p $@

# Appli object compilation
$(APPLI_BUILD)/%.o: $(ROOT)/Appli/Core/Src/%.c | $(APPLI_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(APPLI_INC) $< -o $@

$(APPLI_BUILD)/%.o: $(ROOT)/Drivers/BSP/STM32H7RSxx_Nucleo/%.c | $(APPLI_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(APPLI_INC) $< -o $@

$(APPLI_BUILD)/%.o: $(ROOT)/Drivers/STM32H7RSxx_HAL_Driver/Src/%.c | $(APPLI_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(APPLI_INC) $< -o $@

$(APPLI_BUILD)/%.o: $(ROOT)/gcc/%.c | $(APPLI_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(APPLI_INC) $< -o $@

$(APPLI_BUILD)/%.o: $(ROOT)/Middlewares/Third_Party/FreeRTOS/Source/%.c | $(APPLI_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(APPLI_INC) $< -o $@

$(APPLI_BUILD)/%.o: $(ROOT)/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F/%.c | $(APPLI_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(APPLI_INC) $< -o $@

$(APPLI_BUILD)/%.o: $(ROOT)/Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/%.c | $(APPLI_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(APPLI_INC) $< -o $@

$(APPLI_BUILD)/%.o: $(ROOT)/Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/%.c | $(APPLI_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(APPLI_INC) $< -o $@

$(APPLI_BUILD)/%.o: $(ROOT)/Appli/LWIP/App/%.c | $(APPLI_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(APPLI_INC) $< -o $@

$(APPLI_BUILD)/%.o: $(ROOT)/Appli/LWIP/Target/%.c | $(APPLI_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(APPLI_INC) $< -o $@

$(APPLI_BUILD)/%.o: $(ROOT)/Drivers/BSP/Components/lan8742/%.c | $(APPLI_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(APPLI_INC) $< -o $@

$(APPLI_BUILD)/%.o: $(ROOT)/Middlewares/Third_Party/LwIP/src/core/%.c | $(APPLI_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(APPLI_INC) $< -o $@

$(APPLI_BUILD)/%.o: $(ROOT)/Middlewares/Third_Party/LwIP/src/core/ipv4/%.c | $(APPLI_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(APPLI_INC) $< -o $@

$(APPLI_BUILD)/%.o: $(ROOT)/Middlewares/Third_Party/LwIP/src/api/%.c | $(APPLI_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(APPLI_INC) $< -o $@

$(APPLI_BUILD)/%.o: $(ROOT)/Middlewares/Third_Party/LwIP/src/netif/%.c | $(APPLI_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(APPLI_INC) $< -o $@

$(APPLI_BUILD)/%.o: $(ROOT)/Middlewares/Third_Party/LwIP/system/OS/%.c | $(APPLI_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(APPLI_INC) $< -o $@

$(APPLI_BUILD)/%.o: $(ROOT)/Middlewares/Third_Party/mbedtls/library/%.c | $(APPLI_BUILD)
	@echo "[CC]  $<"
	@$(CC) -c $(COMMON_FLAGS) $(APPLI_INC) $< -o $@

$(APPLI_BUILD)/startup_stm32h7s3xx.o: $(BOOT_STARTUP) | $(APPLI_BUILD)
	@echo "[AS]  $<"
	@$(AS) -c $(CPU_FLAGS) -g3 $< -o $@

$(APPLI_BUILD):
	@mkdir -p $@

##############################################################################
# Flash targets
##############################################################################
flash: flash-boot flash-appli

flash-boot: $(BOOT_ELF)
	@echo "[FLASH] Boot -> internal flash"
	"$(PROGRAMMER)" -c port=SWD freq=8000 -w $(BOOT_ELF) -hardRst

flash-appli: $(APPLI_ELF)
	@echo "[FLASH] Appli -> XSPI2 NOR flash"
	"$(PROGRAMMER)" -c port=SWD freq=8000 -el "$(EXT_LOADER)" -w $(APPLI_ELF) -hardRst

## Generate raw binary from Appli ELF
$(APPLI_BIN): $(APPLI_ELF)
	@echo "[BIN]  $@"
	@$(CP) -O binary $< $@

## Sign: build Appli binary, compute HMAC tag, rebuild Boot with new tag
sign: $(APPLI_BIN)
	@echo "[SIGN] Computing HMAC-SHA256 tag..."
	@python3 scripts/sign_firmware.py
	@rm -f $(BOOT_BUILD)/secure_boot.o
	@$(MAKE) boot

## Full secure build + flash both images
secure-flash: sign flash-boot flash-appli
	@echo "[secure-flash] Done — board running signed firmware."

clean:
	rm -rf build/

##############################################################################
# Monitoring targets — each opens a dedicated terminal window
##############################################################################

## Start the Python telemetry server (stores POSTed JSON in metrics.db)
server:
	@echo "[server] Opening telemetry server window..."
	cmd //c start "Telemetry Server :5000" cmd //k "python $(SERVER_PY)"

## Open a serial terminal on COM6 @ 115200 (requires pyserial: pip install pyserial)
uart:
	@echo "[uart] Opening $(UART_PORT) @ $(UART_BAUD)..."
	cmd //c start "UART $(UART_PORT)" cmd //k "python -m serial.tools.miniterm $(UART_PORT) $(UART_BAUD) --raw --eol LF"

## Continuous ping to the board so you can see when Ethernet is alive
ping:
	@echo "[ping] Pinging $(BOARD_IP) continuously..."
	cmd //c start "Ping $(BOARD_IP)" cmd //k "ping -t $(BOARD_IP)"

## Open all three monitoring windows at once
monitor: server uart ping
	@echo "[monitor] Server + UART + Ping windows launched."

##############################################################################
# Workflow shortcuts
##############################################################################

## Build everything, flash both images, then open all monitoring windows
run: all flash monitor
	@echo "[run] Build complete, board flashed, monitoring started."

## Flash only (no rebuild), then open all monitoring windows
## Useful when firmware is already built and you just want to re-flash + watch
flash-run: flash monitor
	@echo "[flash-run] Board flashed, monitoring started."
