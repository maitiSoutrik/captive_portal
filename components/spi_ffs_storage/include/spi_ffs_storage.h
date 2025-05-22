#ifndef SPI_FFS_STORAGE_H
#define SPI_FFS_STORAGE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"



void spi_ffs_storage_init();
void spi_ffs_storage_test();

#endif