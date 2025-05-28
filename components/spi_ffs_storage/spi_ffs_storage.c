#include "spi_ffs_storage.h"
#include "esp_err.h" // Added for esp_err_t

#define TAG "SPI_FFS_STORAGE"

esp_err_t spi_ffs_storage_init() { // Changed return type to esp_err_t
    // Initialize SPIFFS
    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 5,
      .format_if_mount_failed = true
    };

    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret; // Return the error code
    }
    
    ESP_LOGI(TAG, "Performing SPIFFS_check().");
    
    ret = esp_spiffs_check(conf.partition_label);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS_check() failed (%s)", esp_err_to_name(ret));
        // esp_vfs_spiffs_unregister might be needed here if spiffs_register succeeded but check failed
        // For simplicity, just returning the error from check.
        // Consider unregistering if spiffs was registered: esp_vfs_spiffs_unregister(conf.partition_label);
        return ret; // Return the error code
    } else {
        ESP_LOGI(TAG, "SPIFFS_check() successful");
    }

    size_t total = 0, used = 0;

    ret = esp_spiffs_info(conf.partition_label, &total, &used);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s). Formatting...", esp_err_to_name(ret));
        // Attempt to format, but this might also fail.
        // Consider if formatting should be automatic or a specific recovery step.
        // For now, just log and return the error from esp_spiffs_info.
        // esp_spiffs_format(conf.partition_label); // This itself can fail.
        // esp_vfs_spiffs_unregister(conf.partition_label); // Consider unregistering
        return ret; // Return the error code
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    // Check consistency of reported partiton size info.
    if (used > total) {
        ESP_LOGW(TAG, "Number of used bytes cannot be larger than total. Performing SPIFFS_check().");
        ret = esp_spiffs_check(conf.partition_label);
        // Could be also used to mend broken files, to clean unreferenced pages, etc.
        // More info at https://github.com/pellepl/spiffs/wiki/FAQ#powerlosses-contd-when-should-i-run-spiffs_check
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SPIFFS_check() failed (%s)", esp_err_to_name(ret));
            // esp_vfs_spiffs_unregister(conf.partition_label); // Consider unregistering
            return ret; // Return the error code
        } else {
            ESP_LOGI(TAG, "SPIFFS_check() successful");
        }
    }

    spi_ffs_storage_list_files();
    return ESP_OK; // Success
}

void spi_ffs_storage_test()
{
    // Use POSIX and C standard library functions to work with files.
    // First create a file.
    ESP_LOGI(TAG, "Opening file");
    FILE* f = fopen("/spiffs/hello.txt", "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return;
    }
    fprintf(f, "Hello World!\n");
    fclose(f);
    ESP_LOGI(TAG, "File written");

    // Check if destination file exists before renaming
    struct stat st;
    if (stat("/spiffs/foo.txt", &st) == 0) {
        // Delete it if it exists
        unlink("/spiffs/foo.txt");
    }

    // Rename original file
    ESP_LOGI(TAG, "Renaming file");
    if (rename("/spiffs/hello.txt", "/spiffs/foo.txt") != 0) {
        ESP_LOGE(TAG, "Rename failed");
        return;
    }

    // Open renamed file for reading
    ESP_LOGI(TAG, "Reading file");
    f = fopen("/spiffs/foo.txt", "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return;
    }
    char line[64];
    fgets(line, sizeof(line), f);
    fclose(f);
    // strip newline
    char* pos = strchr(line, '\n');
    if (pos) {
        *pos = '\0';
    }
    ESP_LOGI(TAG, "Read from file: '%s'", line);

    // All done, unmount partition and disable SPIFFS
    // esp_vfs_spiffs_unregister(conf.partition_label);
    // ESP_LOGI(TAG, "SPIFFS unmounted"); 
}

bool spi_ffs_storage_create_file(const char *filename) 
{
    FILE* f = fopen(filename, "w");
    
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for writing");
        
        return false;
    }
    
    fclose(f);
    
    return true;
}

bool spi_ffs_storage_list_files(void) 
{
    DIR* dir = opendir("/spiffs");

    ESP_LOGI(TAG, "Directory contents:");

    if(dir == NULL) 
    {
        ESP_LOGE(TAG, "Failed to open directory");
        return false;
    }
    
    
    struct dirent* ent;
    
    while ((ent = readdir(dir)) != NULL)
    {
        ESP_LOGI(TAG, "%s File: '%s'", __func__ , ent->d_name);
    }
    
    closedir(dir);

    ESP_LOGI(TAG, "Listed files successfully."); 

    return true;
}

bool spi_ffs_file_write(const char *filename, const char *data, bool append)
{
    if (filename == NULL || data == NULL) {
        ESP_LOGE(TAG, "Invalid parameters for file write");
        return false;
    }

    // Construct full path
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "/spiffs/%s", filename);

    // Open file in write or append mode
    const char *mode = append ? "a" : "w";
    FILE *f = fopen(full_path, mode);
    
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file '%s' for %s", full_path, append ? "appending" : "writing");
        return false;
    }

    // Write data to file
    size_t data_len = strlen(data); // Take the data length as an argument instead of calculating it here
    size_t written = fwrite(data, 1, data_len, f);
    
    fclose(f);

    if (written != data_len) {
        ESP_LOGE(TAG, "Failed to write complete data to file '%s'", full_path);
        return false;
    }

    ESP_LOGI(TAG, "Successfully %s %zu bytes to file '%s'", 
             append ? "appended" : "wrote", written, full_path);
    return true;
}

bool spi_ffs_file_read(const char *filename, char *buffer, size_t buffer_size)
{
    if (filename == NULL || buffer == NULL || buffer_size == 0) {
        ESP_LOGE(TAG, "Invalid parameters for file read");
        return false;
    }

    // Construct full path
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "/spiffs/%s", filename);

    FILE *f = fopen(full_path, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file '%s' for reading", full_path);
        return false;
    }

    // Read data from file
    size_t bytes_read = fread(buffer, 1, buffer_size - 1, f); // 2nd param is size of each element (1 byte in this case) and 3rd param is number of elements to read
    fclose(f);

    // Null-terminate the buffer
    buffer[bytes_read] = '\0';

    ESP_LOGI(TAG, "Successfully read %zu bytes from file '%s'", bytes_read, full_path);
    return true;
}

bool spi_ffs_file_delete(const char *filename)
{
    if (filename == NULL) {
        ESP_LOGE(TAG, "Invalid filename for file delete");
        return false;
    }

    // Construct full path
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "/spiffs/%s", filename);

    // Delete the file
    if (unlink(full_path) != 0) {
        ESP_LOGE(TAG, "Failed to delete file '%s'", full_path);
        return false;
    }

    ESP_LOGI(TAG, "Successfully deleted file '%s'", full_path);
    return true;
}

bool spi_ffs_file_exists(const char *filename)
{
    if (filename == NULL) {
        ESP_LOGE(TAG, "Invalid filename for file exists check");
        return false;
    }

    // Construct full path
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "/spiffs/%s", filename);

    struct stat st;
    return (stat(full_path, &st) == 0);
}

int32_t spi_ffs_file_size(const char *filename)
{
    if (filename == NULL) {
        ESP_LOGE(TAG, "Invalid filename for file size check");
        return -1;
    }

    // Construct full path
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "/spiffs/%s", filename);

    struct stat st;
    if (stat(full_path, &st) != 0) {
        ESP_LOGE(TAG, "Failed to get file size for '%s'", full_path);
        return -1;
    }

    ESP_LOGI(TAG, "File '%s' size: %ld bytes", full_path, st.st_size);
    return (int32_t)st.st_size;
}

esp_err_t spi_ffs_rename(const char *old_filename, const char *new_filename)
{
    if (old_filename == NULL || new_filename == NULL) {
        ESP_LOGE(TAG, "Invalid parameters for file rename");
        return ESP_ERR_INVALID_ARG;
    }

    // Construct full paths
    char old_path[256];
    char new_path[256];
    snprintf(old_path, sizeof(old_path), "/spiffs/%s", old_filename);
    snprintf(new_path, sizeof(new_path), "/spiffs/%s", new_filename);

    // Check if destination file exists and delete it
    struct stat st;
    if (stat(new_path, &st) == 0) {
        if (unlink(new_path) != 0) {
            ESP_LOGE(TAG, "Failed to delete existing destination file '%s'", new_path);
            return ESP_FAIL;
        }
    }

    // Rename the file
    if (rename(old_path, new_path) != 0) {
        ESP_LOGE(TAG, "Failed to rename file from '%s' to '%s'", old_path, new_path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Successfully renamed file from '%s' to '%s'", old_path, new_path);
    return ESP_OK;
}

bool spi_ffs_read_file_line(const char *filename, char *buffer, size_t buffer_size)
{
    if (filename == NULL || buffer == NULL || buffer_size == 0) {
        ESP_LOGE(TAG, "Invalid parameters for file line read");
        return false;
    }

    // Construct full path
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "/spiffs/%s", filename);

    FILE *f = fopen(full_path, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file '%s' for reading", full_path);
        return false;
    }

    // Read one line from the file
    if (fgets(buffer, buffer_size, f) == NULL) {
        ESP_LOGE(TAG, "Failed to read line from file '%s'", full_path);
        fclose(f);
        return false;
    }

    fclose(f);

    // Remove newline character if present
    char *pos = strchr(buffer, '\n');
    if (pos) {
        *pos = '\0';
    }

    ESP_LOGI(TAG, "Successfully read line from file '%s': '%s'", full_path, buffer);
    return true;
}

void spi_ffs_storage_test_all_functions(void)
{
    ESP_LOGI(TAG, "=== Starting comprehensive SPIFFS API test ===");
    
    const char *test_filename = "test_file.txt";
    const char *test_data = "This is test data for SPIFFS storage.\n";
    const char *append_data = "This line was appended.\n";
    const char *renamed_filename = "renamed_test.txt";
    char read_buffer[256];
    
    // Test 1: File write (create new file)
    ESP_LOGI(TAG, "Test 1: Writing to new file");
    if (spi_ffs_file_write(test_filename, test_data, false)) {
        ESP_LOGI(TAG, "✓ File write test PASSED");
    } else {
        ESP_LOGE(TAG, "✗ File write test FAILED");
        return;
    }
    
    // Test 2: File exists check
    ESP_LOGI(TAG, "Test 2: Checking if file exists");
    if (spi_ffs_file_exists(test_filename)) {
        ESP_LOGI(TAG, "✓ File exists test PASSED");
    } else {
        ESP_LOGE(TAG, "✗ File exists test FAILED");
        return;
    }
    
    // Test 3: File size check
    ESP_LOGI(TAG, "Test 3: Getting file size");
    int32_t file_size = spi_ffs_file_size(test_filename);
    if (file_size > 0) {
        ESP_LOGI(TAG, "✓ File size test PASSED (size: %ld bytes)", file_size);
    } else {
        ESP_LOGE(TAG, "✗ File size test FAILED");
        return;
    }
    
    // Test 4: File read
    ESP_LOGI(TAG, "Test 4: Reading file contents");
    memset(read_buffer, 0, sizeof(read_buffer));
    if (spi_ffs_file_read(test_filename, read_buffer, sizeof(read_buffer))) {
        ESP_LOGI(TAG, "✓ File read test PASSED");
        ESP_LOGI(TAG, "Read content: '%s'", read_buffer);
    } else {
        ESP_LOGE(TAG, "✗ File read test FAILED");
        return;
    }
    
    // Test 5: File append
    ESP_LOGI(TAG, "Test 5: Appending to file");
    if (spi_ffs_file_write(test_filename, append_data, true)) {
        ESP_LOGI(TAG, "✓ File append test PASSED");
    } else {
        ESP_LOGE(TAG, "✗ File append test FAILED");
        return;
    }
    
    // Test 6: Read file after append
    ESP_LOGI(TAG, "Test 6: Reading file after append");
    memset(read_buffer, 0, sizeof(read_buffer));
    if (spi_ffs_file_read(test_filename, read_buffer, sizeof(read_buffer))) {
        ESP_LOGI(TAG, "✓ File read after append test PASSED");
        ESP_LOGI(TAG, "Updated content: '%s'", read_buffer);
    } else {
        ESP_LOGE(TAG, "✗ File read after append test FAILED");
        return;
    }
    
    // Test 7: Read first line only
    ESP_LOGI(TAG, "Test 7: Reading first line only");
    memset(read_buffer, 0, sizeof(read_buffer));
    if (spi_ffs_read_file_line(test_filename, read_buffer, sizeof(read_buffer))) {
        ESP_LOGI(TAG, "✓ Read line test PASSED");
        ESP_LOGI(TAG, "First line: '%s'", read_buffer);
    } else {
        ESP_LOGE(TAG, "✗ Read line test FAILED");
        return;
    }
    
    // Test 8: File rename
    ESP_LOGI(TAG, "Test 8: Renaming file");
    if (spi_ffs_rename(test_filename, renamed_filename) == ESP_OK) {
        ESP_LOGI(TAG, "✓ File rename test PASSED");
    } else {
        ESP_LOGE(TAG, "✗ File rename test FAILED");
        return;
    }
    
    // Test 9: Check old file doesn't exist, new file exists
    ESP_LOGI(TAG, "Test 9: Verifying rename operation");
    if (!spi_ffs_file_exists(test_filename) && spi_ffs_file_exists(renamed_filename)) {
        ESP_LOGI(TAG, "✓ Rename verification test PASSED");
    } else {
        ESP_LOGE(TAG, "✗ Rename verification test FAILED");
        return;
    }
    
    // Test 10: Read renamed file
    ESP_LOGI(TAG, "Test 10: Reading renamed file");
    memset(read_buffer, 0, sizeof(read_buffer));
    if (spi_ffs_file_read(renamed_filename, read_buffer, sizeof(read_buffer))) {
        ESP_LOGI(TAG, "✓ Read renamed file test PASSED");
        ESP_LOGI(TAG, "Renamed file content: '%s'", read_buffer);
    } else {
        ESP_LOGE(TAG, "✗ Read renamed file test FAILED");
        return;
    }
    
    // Test 11: List files (should show renamed file)
    ESP_LOGI(TAG, "Test 11: Listing files");
    if (spi_ffs_storage_list_files()) {
        ESP_LOGI(TAG, "✓ List files test PASSED");
    } else {
        ESP_LOGE(TAG, "✗ List files test FAILED");
    }
    
    // Test 12: File delete
    ESP_LOGI(TAG, "Test 12: Deleting file");
    if (spi_ffs_file_delete(renamed_filename)) {
        ESP_LOGI(TAG, "✓ File delete test PASSED");
    } else {
        ESP_LOGE(TAG, "✗ File delete test FAILED");
        return;
    }
    
    // Test 13: Verify file is deleted
    ESP_LOGI(TAG, "Test 13: Verifying file deletion");
    if (!spi_ffs_file_exists(renamed_filename)) {
        ESP_LOGI(TAG, "✓ Delete verification test PASSED");
    } else {
        ESP_LOGE(TAG, "✗ Delete verification test FAILED");
        return;
    }
    
    // Test 14: Final file listing (should be empty or show only remaining files)
    ESP_LOGI(TAG, "Test 14: Final file listing");
    spi_ffs_storage_list_files();
    
    ESP_LOGI(TAG, "=== All SPIFFS API tests completed successfully! ===");
}
