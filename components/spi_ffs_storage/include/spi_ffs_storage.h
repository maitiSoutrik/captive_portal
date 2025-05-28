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
#include "dirent.h"


esp_err_t spi_ffs_storage_init(); // Changed return type to esp_err_t
void spi_ffs_storage_test();
/*
File operation APIs
*/

/*
@brief: write data into a file
@param path: Path of the file to write
@param data: Data to be written
@param append: Whether to append to an existing file or overwrite it
@return 1 on success, 0 on failure
*/
bool spi_ffs_file_write(const char *filename, const char *data, bool append);

/*
@brief: Read data from a file into a buffer
@param path: Path of the file to read
@param buffer: Buffer to store the data
@param buffer_size: Size of the buffer
@return 1 on success, 0 on failure
*/
bool spi_ffs_file_read(const char *filename, char *buffer, size_t buffer_size);


/*
@brief: Delete a file from the SPIFFS storage
@param path: Path of the file to delete
@return ESP_OK on success, ESP_ERR_NOT_FOUND if the file does not exist, other error codes otherwise
*/
bool spi_ffs_file_delete(const char *filename);

/*
@brief: Check if file exists
@param path: Path of the file to check
@return true if file exists, false otherwise
*/ 
bool spi_ffs_file_exists(const char *filename);

/*
@brief: Get the size of a file in bytes
@param path: Path of the file to get the size for
@return The size of the file in bytes, or -1 if an error occurred
*/
int32_t spi_ffs_file_size(const char *path);


/*
@brief: rename file
@param old_path: Old path of the file to be renamed
@param new_path: New path of the file after renaming
@return ESP_OK on success, other error codes otherwise
*/ 
esp_err_t spi_ffs_rename(const char *old_path, const char *new_path);


bool spi_ffs_read_file_line(const char *filename, char *buffer, size_t buffer_size);

bool spi_ffs_storage_create_file(const char *filename);

bool spi_ffs_storage_list_files(void);

void spi_ffs_storage_test_all_functions(void);

#endif
