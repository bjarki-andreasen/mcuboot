/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 * Copyright (c) 2015 Runtime Inc
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>

#include "target.h"

#include <flash_map_backend/flash_map_backend.h>
#include <sysflash/sysflash.h>

#include "bootutil/boot_hooks.h"
#include "bootutil/bootutil_log.h"
#include "bootutil/bootutil_public.h"

BOOT_LOG_MODULE_DECLARE(mcuboot);

#if defined(CONFIG_STM32_MEMMAP)
/* MEMORY MAPPED for XiP on external NOR flash takes the sspi-nor or ospi-nor or qspi-nor device */
#define FLASH_DEVICE_ID SPI_FLASH_0_ID
#if DT_NODE_HAS_STATUS(DT_INST(0, st_stm32_xspi_nor), okay)
#define DT_DRV_COMPAT st_stm32_xspi_nor
#define FLASH_DEVICE_NODE DT_INST(0, st_stm32_xspi_nor)
#define FLASH_DEVICE_BASE DT_REG_ADDR_BY_IDX(DT_INST_PARENT(0), 1)
#elif DT_NODE_HAS_STATUS(DT_INST(0, st_stm32_ospi_nor), okay)
#define DT_DRV_COMPAT st_stm32_ospi_nor
#define FLASH_DEVICE_NODE DT_INST(0, st_stm32_ospi_nor)
#define FLASH_DEVICE_BASE DT_REG_ADDR_BY_IDX(DT_INST_PARENT(0), 1)
#elif DT_NODE_HAS_STATUS(DT_INST(0, st_stm32_qspi_nor), okay)
#define DT_DRV_COMPAT st_stm32_qspi_nor
#define FLASH_DEVICE_NODE DT_INST(0, st_stm32_qspi_nor)
#define FLASH_DEVICE_BASE DT_REG_ADDR_BY_IDX(DT_INST_PARENT(0), 1)
#else
#error "FLASH_DEVICE_NODE could not be determined"
#endif

#elif (!defined(CONFIG_XTENSA) && DT_HAS_CHOSEN(zephyr_flash_controller))
#define FLASH_DEVICE_ID SOC_FLASH_0_ID
#define FLASH_DEVICE_BASE CONFIG_FLASH_BASE_ADDRESS
#define FLASH_DEVICE_NODE DT_CHOSEN(zephyr_flash_controller)

#elif (defined(CONFIG_XTENSA) && DT_NODE_EXISTS(DT_INST(0, jedec_spi_nor)))
#define FLASH_DEVICE_ID SPI_FLASH_0_ID
#define FLASH_DEVICE_BASE 0
#define FLASH_DEVICE_NODE DT_INST(0, jedec_spi_nor)

#elif defined(CONFIG_SOC_FAMILY_ESPRESSIF_ESP32)

#define FLASH_DEVICE_ID SPI_FLASH_0_ID
#define FLASH_DEVICE_BASE 0
#define FLASH_DEVICE_NODE DT_CHOSEN(zephyr_flash_controller)

#elif (defined(CONFIG_SOC_SERIES_NRF54HX) && DT_HAS_CHOSEN(zephyr_flash))

#define FLASH_DEVICE_ID SPI_FLASH_0_ID
#define FLASH_DEVICE_BASE CONFIG_FLASH_BASE_ADDRESS
#define FLASH_DEVICE_NODE DT_CHOSEN(zephyr_flash)

#else
#error "FLASH_DEVICE_ID could not be determined"
#endif

static const struct device *flash_dev = DEVICE_DT_GET(FLASH_DEVICE_NODE);

int flash_device_base(uint8_t fd_id, uintptr_t *ret)
{
    if (fd_id != FLASH_DEVICE_ID) {
        BOOT_LOG_ERR("invalid flash ID %d; expected %d",
                     fd_id, FLASH_DEVICE_ID);
        return -EINVAL;
    }
    *ret = FLASH_DEVICE_BASE;
    return 0;
}

/*
 * This depends on the mappings defined in sysflash.h.
 * MCUBoot uses continuous numbering for the primary slot, the secondary slot,
 * and the scratch while zephyr might number it differently.
 */
int flash_area_id_from_multi_image_slot(int image_index, int slot)
{
    int rc;
    int id = -1;

    rc = BOOT_HOOK_FLASH_AREA_CALL(flash_area_id_from_multi_image_slot_hook,
                                   BOOT_HOOK_REGULAR, image_index, slot, &id);
    if (rc != BOOT_HOOK_REGULAR) {
        return id;
    }

    switch (slot) {
    case 0: return FLASH_AREA_IMAGE_PRIMARY(image_index);
#if !defined(CONFIG_SINGLE_APPLICATION_SLOT)
    case 1: return FLASH_AREA_IMAGE_SECONDARY(image_index);
#endif
    }

    return -EINVAL; /* flash_area_open will fail on that */
}

int flash_area_id_from_image_slot(int slot)
{
    return flash_area_id_from_multi_image_slot(0, slot);
}

int flash_area_id_to_multi_image_slot(int image_index, int area_id)
{
    if (area_id == FLASH_AREA_IMAGE_PRIMARY(image_index)) {
        return 0;
    }
#if !defined(CONFIG_SINGLE_APPLICATION_SLOT)
    if (area_id == FLASH_AREA_IMAGE_SECONDARY(image_index)) {
        return 1;
    }
#endif

    BOOT_LOG_ERR("invalid flash area ID");
    return -1;
}

#if defined(CONFIG_MCUBOOT_SERIAL_DIRECT_IMAGE_UPLOAD)
int flash_area_id_from_direct_image(int image_id)
{
    switch (image_id) {
    case 0:
    case 1:
        return FIXED_PARTITION_ID(slot0_partition);
#if FIXED_PARTITION_EXISTS(slot1_partition)
    case 2:
        return FIXED_PARTITION_ID(slot1_partition);
#endif
#if FIXED_PARTITION_EXISTS(slot2_partition)
    case 3:
        return FIXED_PARTITION_ID(slot2_partition);
#endif
#if FIXED_PARTITION_EXISTS(slot3_partition)
    case 4:
        return FIXED_PARTITION_ID(slot3_partition);
#endif
#if FIXED_PARTITION_EXISTS(slot4_partition)
    case 5:
        return FIXED_PARTITION_ID(slot4_partition);
#endif
#if FIXED_PARTITION_EXISTS(slot5_partition)
    case 6:
        return FIXED_PARTITION_ID(slot5_partition);
#endif
    }
    return -EINVAL;
}
#endif

int flash_area_sector_from_off(off_t off, struct flash_sector *sector)
{
    int rc;
    struct flash_pages_info page;

    rc = flash_get_page_info_by_offs(flash_dev, off, &page);
    if (rc) {
        return rc;
    }

    sector->fs_off = page.start_offset;
    sector->fs_size = page.size;

    return rc;
}

uint8_t flash_area_get_device_id(const struct flash_area *fa)
{
    (void)fa;
    return FLASH_DEVICE_ID;
}

#define ERASED_VAL 0xff
__weak uint8_t flash_area_erased_val(const struct flash_area *fap)
{
    (void)fap;
    return ERASED_VAL;
}

int flash_area_get_sector(const struct flash_area *fap, off_t off,
                          struct flash_sector *fsp)
{
    struct flash_pages_info fpi;
    int rc;

    if (off < 0 || (size_t) off >= fap->fa_size) {
        return -ERANGE;
    }

    rc = flash_get_page_info_by_offs(fap->fa_dev, fap->fa_off + off,
            &fpi);

    if (rc == 0) {
        fsp->fs_off = fpi.start_offset - fap->fa_off;
        fsp->fs_size = fpi.size;
    }

    return rc;
}
