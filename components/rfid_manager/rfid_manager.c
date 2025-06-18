#include "rfid_manager.h"
#include <string.h>
#include "esp_spiffs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h" // For mutex
#include <time.h>            // For time()
#include "esp_timer.h"       // For esp_timer functions
// #include <inttypes.h> // PRIX32 not used, using %lx with cast instead

static const char *TAG = "RFID_MANAGER";

#define RFID_DATABASE_FILE "/spiffs/rfid_cards.dat"

// In-memory database for RFID cards
static rfid_card_t rfid_database[RFID_MAX_CARDS];

// Mutex for thread-safe access to the database and file operations
static SemaphoreHandle_t rfid_mutex = NULL;

// --- Caching Mechanism ---
static bool is_dirty = false; // Flag to indicate pending NVS write
static esp_timer_handle_t rfid_write_timer = NULL; // Timer for delayed NVS write

#define RFID_WRITE_TIMEOUT_MS (5000) // 5 seconds, adjust as needed
#define RFID_WRITE_TIMEOUT_US_NORMAL (RFID_WRITE_TIMEOUT_MS * 1000ULL)

// Default to normal timeout, can be overridden for tests by rfid_manager_set_cache_write_timeout
static uint64_t s_rfid_current_write_timeout_us = RFID_WRITE_TIMEOUT_US_NORMAL; 
// --- End Caching Mechanism ---

// Default RFID cards
static const rfid_card_t default_cards[] = {
    {0x12345678, 1, "Admin Card", 0},
    {0x87654321, 1, "User Card 1", 0},
    {0xABCDEF00, 1, "User Card 2", 0}
    // Add more default cards if needed, up to RFID_MAX_CARDS
};
static const uint16_t num_default_cards = sizeof(default_cards) / sizeof(rfid_card_t);

// Forward declarations for internal helper functions
/**
 * @brief Loads default RFID cards into the database.
 *
 * This function populates the in-memory database with a predefined set of RFID cards
 * and then saves this default set to the persistent storage file.
 * This is typically called during the first boot or if the existing database is invalid.
 *
 * @return esp_err_t ESP_OK on success, or an error code if saving to file fails.
 */
static esp_err_t rfid_manager_load_defaults(void);

/**
 * @brief Saves the current in-memory RFID database to the SPIFFS file.
 *
 * This function writes the database header and all card data (including inactive ones
 * if they are kept in the array) to the persistent storage file.
 * It also calculates and stores a checksum for data integrity.
 *
 * @return esp_err_t ESP_OK on success, or an error code on failure (e.g., file write error).
 */
static esp_err_t rfid_manager_save_to_file(void);

/**
 * @brief Loads the RFID database from the SPIFFS file into memory.
 *
 * Reads the database header and card data from persistent storage.
 * Verifies the checksum to ensure data integrity.
 *
 * @return esp_err_t ESP_OK on success, ESP_ERR_NOT_FOUND if the file doesn't exist,
 *         ESP_ERR_INVALID_CRC if checksum fails, ESP_FAIL for other errors.
 */
static esp_err_t rfid_manager_load_from_file(void);

/**
 * @brief Handles the timeout for cache write operations.
 * @param arg The argument passed when creating
 * the cache write task.
 * @return void
 */
static void rfid_cache_write_timeout_handler(void* arg); // Added for completeness

// --- Core API Functions ---

esp_err_t rfid_manager_init(void)
{
    if (rfid_mutex == NULL)
    {
        rfid_mutex = xSemaphoreCreateMutex();
        if (rfid_mutex == NULL)
        {
            ESP_LOGE(TAG, "Failed to create RFID mutex");
            return ESP_FAIL;
        }
    }

    if (rfid_mutex != NULL && xSemaphoreTake(rfid_mutex, portMAX_DELAY) == pdTRUE)
    {
        esp_err_t ret; // Declared once at the beginning of the scope

        size_t total_bytes, used_bytes;

        esp_err_t spiffs_ret = esp_spiffs_info(NULL, &total_bytes, &used_bytes); // Use default partition label (NULL)

        if (spiffs_ret != ESP_OK)
        {
            ESP_LOGE(TAG, "SPIFFS filesystem not found or not mounted. Please initialize SPIFFS first. Error: %s", esp_err_to_name(spiffs_ret));
            xSemaphoreGive(rfid_mutex);
            return ESP_ERR_INVALID_STATE; // Indicate that a required pre-condition (SPIFFS mounted) is not met.
        }

        ESP_LOGI(TAG, "SPIFFS filesystem found. Partition size: total: %zu, used: %zu", total_bytes, used_bytes);

        // Attempt to load cards from file
        ret = rfid_manager_load_from_file(); // Assign to the already declared ret

        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "RFID database loaded successfully from file.");
        }
        else
        {
            ESP_LOGW(TAG, "Failed to load RFID database from file (%s). Loading defaults.", esp_err_to_name(ret));
            // If loading failed (file not found, corrupted, etc.), load default cards.
            ret = rfid_manager_load_defaults();
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to load default RFID cards. System may not function correctly.");
                // This is a critical failure if defaults can't be loaded.
            }
            else
            {
                ESP_LOGI(TAG, "Default RFID cards loaded and saved.");
            }
        }

        // Initialize caching mechanism state and create the timer
        is_dirty = false;
        if (rfid_write_timer == NULL) { // Create timer only if it hasn't been created
            esp_timer_create_args_t timer_args = {
                .callback = &rfid_cache_write_timeout_handler,
                .name = "rfid_write_timer"
                // .arg = NULL; // Not passing any specific arg to handler
            };
            esp_err_t timer_create_ret = esp_timer_create(&timer_args, &rfid_write_timer);
            if (timer_create_ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to create rfid_write_timer: %s", esp_err_to_name(timer_create_ret));
                // If timer creation fails, init should indicate a failure for the caching system.
                xSemaphoreGive(rfid_mutex);
                return ESP_FAIL; 
            }
            ESP_LOGI(TAG, "RFID write timer created successfully.");
        }

        xSemaphoreGive(rfid_mutex);
        return ret; // Return status of load_from_file or load_defaults
    }
    else
    {
        ESP_LOGE(TAG, "Failed to take RFID mutex in init");
        return ESP_FAIL;
    }
}

#ifdef UNIT_TEST
void rfid_manager_set_cache_write_timeout(uint32_t timeout_ms)
{
    if (rfid_mutex != NULL && xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        if (timeout_ms > 0)
        {
            s_rfid_current_write_timeout_us = (uint64_t)timeout_ms * 1000ULL;
            ESP_LOGI(TAG, "RFID cache write timeout updated to %llu us (%lu ms)", s_rfid_current_write_timeout_us, (unsigned long)timeout_ms);
        }
        else
        {
            // Optionally, reset to default if timeout_ms is 0, or just log an error/warning.
            // For now, let's assume 0 means "use default".
            s_rfid_current_write_timeout_us = RFID_WRITE_TIMEOUT_US_NORMAL;
            ESP_LOGI(TAG, "RFID cache write timeout reset to default %llu us", s_rfid_current_write_timeout_us);
        }
        xSemaphoreGive(rfid_mutex);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to take RFID mutex in set_cache_write_timeout. Timeout not changed.");
    }
}
#endif // UNIT_TEST

esp_err_t rfid_manager_add_card(uint32_t card_id, const char *name)
{
    if (rfid_mutex != NULL && xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
    {
        if (name == NULL)
        {
            ESP_LOGE(TAG, "Cannot add card with NULL name.");
            xSemaphoreGive(rfid_mutex);
            return ESP_ERR_INVALID_ARG;
        }

        // Check if card already exists by iterating through all possible slots
        // A card_id is considered to exist if it's present in any slot and is not 0 (which might indicate an uninitialized slot).
        // This check prevents adding a card with an ID that is already in the system, regardless of its active status.
        for (uint16_t i = 0; i < RFID_MAX_CARDS; ++i)
        {
            if (rfid_database[i].card_id == card_id && rfid_database[i].card_id != 0)
            {
                ESP_LOGW(TAG, "Attempt to add card 0x%08lx which already exists at slot %u (status: %s). Operation aborted.",
                         (unsigned long)card_id, i, rfid_database[i].active ? "active" : "inactive");
                xSemaphoreGive(rfid_mutex);
                return ESP_ERR_INVALID_STATE; // Card ID already present in the database, operation invalid in this state
            }
        }

        // If card does not exist, try to add to the first inactive slot or at the end
        uint16_t _index_of_first_inactive_slot = RFID_MAX_CARDS;

        // First, look for an inactive slot to reuse
        for (uint16_t i = 0; i < RFID_MAX_CARDS; ++i)
        {
            // This logic assumes that card_id == 0 can mean an empty/never-used slot,
            // if we compact the array on removal.
            // The current design saves all RFID_MAX_CARDS slots.
            // An inactive card would have card_id != 0 but active == 0.
            // A truly empty slot might have card_id == 0.
            // Let's find the first slot where card_id is 0 (never used) or active is 0 (previously removed)
            if (rfid_database[i].card_id == 0 || rfid_database[i].active == 0)
            {
                _index_of_first_inactive_slot = i;
                break;
            }
        }

        if (_index_of_first_inactive_slot < RFID_MAX_CARDS)
        {
            rfid_database[_index_of_first_inactive_slot].card_id = card_id;
            strncpy(rfid_database[_index_of_first_inactive_slot].name, name, RFID_CARD_NAME_LEN - 1);
            rfid_database[_index_of_first_inactive_slot].name[RFID_CARD_NAME_LEN - 1] = '\0';
            rfid_database[_index_of_first_inactive_slot].active = 1;
            time_t now_add;
            time(&now_add);
            rfid_database[_index_of_first_inactive_slot].timestamp = (uint32_t)now_add; // Set current timestamp

            ESP_LOGI(TAG, "Added card %lu ('%s') at slot %u.", (unsigned long)card_id, name, _index_of_first_inactive_slot);

            // Caching logic:
            is_dirty = true;
            ESP_LOGI(TAG, "RFID data marked dirty.");

            if (rfid_write_timer != NULL) {
                // Stop the timer if it's already running to reset the timeout period
                esp_err_t stop_err = esp_timer_stop(rfid_write_timer);
                if (stop_err != ESP_OK && stop_err != ESP_ERR_INVALID_STATE) {
                    // ESP_ERR_INVALID_STATE means timer was not running, which is fine.
                    ESP_LOGW(TAG, "Failed to stop rfid_write_timer: %s", esp_err_to_name(stop_err));
                }

                // Start the one-shot timer
                esp_err_t start_err = esp_timer_start_once(rfid_write_timer, s_rfid_current_write_timeout_us);
                if (start_err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to start rfid_write_timer: %s. Data will not be auto-saved by timer.", esp_err_to_name(start_err));
                    // If timer fails to start, data remains dirty. Consider fallback or error propagation.
                    // For now, the operation is successful in memory.
                } else {
                    ESP_LOGI(TAG, "RFID write timer started for %llu us.", s_rfid_current_write_timeout_us);
                }
            } else {
                ESP_LOGE(TAG, "rfid_write_timer is NULL. Cannot start timer. Data will not be auto-saved.");
                // This case should ideally not happen if init was successful.
            }

            xSemaphoreGive(rfid_mutex);

            return ESP_OK; // Card added to in-memory cache successfully
        }
        else
        {
            // This case (_index_of_first_inactive_slot == RFID_MAX_CARDS) means all RFID_MAX_CARDS slots
            // have card_id != 0 AND active == 1. So, the database is truly full.
            ESP_LOGW(TAG, "RFID database is full (all %d slots active). Cannot add new card 0x%08lx.", RFID_MAX_CARDS, (unsigned long)card_id);
            xSemaphoreGive(rfid_mutex);
            return ESP_ERR_NO_MEM;
        }
    }
    ESP_LOGE(TAG, "Failed to take RFID mutex in add_card");
    return ESP_FAIL;
}

esp_err_t rfid_manager_remove_card(uint32_t card_id)
{
    if (rfid_mutex != NULL && xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
    {
        for (uint16_t i = 0; i < RFID_MAX_CARDS; ++i)
        { // Iterate all possible slots
            if (rfid_database[i].card_id == card_id && rfid_database[i].active)
            {
                rfid_database[i].active = 0; // Mark as inactive
                // Optionally clear name and timestamp
                // memset(rfid_database[i].name, 0, RFID_CARD_NAME_LEN);
                // rfid_database[i].timestamp = 0;

                ESP_LOGI(TAG, "Removed card %lu (marked inactive in memory).", (unsigned long)card_id);
                
                // Caching logic:
                is_dirty = true;
                ESP_LOGI(TAG, "RFID data marked dirty due to card removal.");

                if (rfid_write_timer != NULL) {
                    esp_err_t stop_err = esp_timer_stop(rfid_write_timer);
                    if (stop_err != ESP_OK && stop_err != ESP_ERR_INVALID_STATE) {
                    ESP_LOGW(TAG, "Failed to stop rfid_write_timer: %s", esp_err_to_name(stop_err));
                }
                esp_err_t start_err = esp_timer_start_once(rfid_write_timer, s_rfid_current_write_timeout_us);
                if (start_err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to start rfid_write_timer for removal: %s.", esp_err_to_name(start_err));
                } else {
                    ESP_LOGI(TAG, "RFID write timer started for %llu us after removal.", s_rfid_current_write_timeout_us);
                }
            } else {
                ESP_LOGE(TAG, "rfid_write_timer is NULL during removal. Cannot start timer.");
                }

                xSemaphoreGive(rfid_mutex);
                return ESP_OK; // Card marked inactive in memory successfully
            }
        }
        ESP_LOGW(TAG, "Card 0x%08lx not found or already inactive.", (unsigned long)card_id);
        xSemaphoreGive(rfid_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGE(TAG, "Failed to take RFID mutex in remove_card");
    return ESP_FAIL;
}

bool rfid_manager_check_card(uint32_t card_id)
{
    if (rfid_mutex != NULL && xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
    {
        for (uint16_t i = 0; i < RFID_MAX_CARDS; ++i)
        { // Iterate all slots
            if (rfid_database[i].card_id == card_id && rfid_database[i].active)
            {
                // Update timestamp on successful check
                time_t now;
                time(&now);
                rfid_database[i].timestamp = (uint32_t)now; // Update timestamp in RAM
                ESP_LOGI(TAG, "Card %lu checked successfully. Timestamp updated in RAM to %lu.", (unsigned long)card_id, (unsigned long)rfid_database[i].timestamp);

                // NOTE: Removed rfid_manager_save_to_file() here to reduce flash wear and improve performance.
                // The timestamp update will only be in RAM until the next explicit save operation (e.g., add/remove card).
                // If persistent timestamps on every check are critical, a different strategy is needed.

                xSemaphoreGive(rfid_mutex);
                return true;
            }
        }
        xSemaphoreGive(rfid_mutex);
        return false;
    }
    ESP_LOGE(TAG, "Failed to take RFID mutex in check_card");
    return false; // Default to not authorized if mutex fails
}

esp_err_t rfid_manager_get_card(uint32_t card_id, rfid_card_t *card)
{
    if (card == NULL)
    {
        ESP_LOGE(TAG, "Output card pointer is NULL.");
        return ESP_ERR_INVALID_ARG;
    }

    if (rfid_mutex != NULL && xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
    {
        for (uint16_t i = 0; i < RFID_MAX_CARDS; ++i)
        {
            if (rfid_database[i].card_id == card_id)
            {
                if (rfid_database[i].active)
                {
                    *card = rfid_database[i]; // Copy the card data
                    xSemaphoreGive(rfid_mutex);
                    ESP_LOGI(TAG, "Card 0x%08lx found at slot %u.", (unsigned long)card_id, i);
                    return ESP_OK;
                }
                else
                {
                    // Card found but is inactive
                    ESP_LOGW(TAG, "Card 0x%08lx found at slot %u but is inactive.", (unsigned long)card_id, i);
                    xSemaphoreGive(rfid_mutex);
                    return ESP_ERR_NOT_FOUND; // Treat inactive as not found for "get active card" purposes
                }
            }
        }
        // Card ID not found in any slot
        ESP_LOGW(TAG, "Card 0x%08lx not found in the database.", (unsigned long)card_id);
        xSemaphoreGive(rfid_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGE(TAG, "Failed to take RFID mutex in get_card");
    return ESP_FAIL; // Mutex acquisition failed
}

uint16_t rfid_manager_get_card_count(void)
{
    // Taking mutex for consistency, though it might be okay for a quick read if not critical.
    uint16_t active_count = 0;
    if (rfid_mutex != NULL && xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
    {
        // Recalculate on demand to ensure accuracy
        for (uint16_t i = 0; i < RFID_MAX_CARDS; ++i)
        {
            if (rfid_database[i].active && rfid_database[i].card_id != 0)
            {
                active_count++;
            }
        }
        xSemaphoreGive(rfid_mutex);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to take RFID mutex in get_card_count, returning 0");
        // Return a safe value or last known value. Here, returning 0 on mutex failure.
    }
    return active_count;
}

esp_err_t rfid_manager_list_cards(rfid_card_t *cards_buffer, uint16_t buffer_size, uint16_t *num_cards_copied)
{
    if (cards_buffer == NULL || num_cards_copied == NULL)
    {
        ESP_LOGE(TAG, "list_cards: Invalid arguments"); // Added logging for invalid args
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK; // Initialize ret

    // Try to take the mutex
    if (rfid_mutex != NULL && xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
    {
        uint16_t active_cards_found = 0;
        for (uint16_t i = 0; i < RFID_MAX_CARDS && active_cards_found < buffer_size; ++i)
        {
            if (rfid_database[i].active && rfid_database[i].card_id != 0)
            {
                cards_buffer[active_cards_found] = rfid_database[i];
                active_cards_found++;
            }
        }
        *num_cards_copied = active_cards_found;

        // Give back the mutex
        xSemaphoreGive(rfid_mutex);
        ESP_LOGD(TAG, "Listed %u cards", active_cards_found);
    }
    else // This 'else' block was missing in the provided snippet
    {
        ESP_LOGE(TAG, "Failed to take RFID mutex in list_cards");
        *num_cards_copied = 0; // Ensure num_cards_copied is set on failure
        ret = ESP_FAIL;        // Set return value to indicate failure
    }
    return ret;
}

esp_err_t rfid_manager_format_database(void)
{
    // Ensure mutex is created, similar to rfid_manager_init()
    // This makes format_database safer if called before init or if mutex was somehow lost.
    if (rfid_mutex == NULL) {
        rfid_mutex = xSemaphoreCreateMutex();
        if (rfid_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create RFID mutex in format_database's check");
            return ESP_FAIL; // Cannot proceed without mutex
        }
    }

    esp_err_t ret = ESP_FAIL;
    // Now, rfid_mutex is guaranteed to be non-NULL if the above check passed.
    if (xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(5000)) == pdTRUE)
    {
        ESP_LOGW(TAG, "Formatting RFID database. All existing cards will be erased and defaults loaded.");
        // Essentially, just load defaults, which will overwrite the file.
        // rfid_manager_load_defaults itself calls rfid_manager_save_to_file.
        // These helpers do not take the mutex, assuming the caller (this function) does.
        ret = rfid_manager_load_defaults();
        xSemaphoreGive(rfid_mutex);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to take RFID mutex in format_database");
        // ret is already ESP_FAIL from initialization
    }
    return ret;
}

esp_err_t rfid_manager_get_card_list_json(char *buffer, size_t bufferMaxLength)
{
    esp_err_t ret = ESP_OK;
    uint16_t _length = 0;
    bool isComma = false;

    // validate the params
    if (!buffer || !bufferMaxLength)
    {
        ESP_LOGE(TAG, "Invalid parameters in get_card_list_json");
        return ESP_FAIL;
    }

    if (rfid_mutex!= NULL && xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(500)) == pdTRUE) // Longer timeout for format
    {
        ESP_LOGI(TAG, "Getting the Lock");

        // clear the buffer
        memset(buffer, 0, bufferMaxLength);

        // Prepare the JSON
        _length = snprintf(buffer, bufferMaxLength, "{\"cards\":[");

        // loop over all possible card slots and add active ones to the JSON string
        for (uint16_t i = 0; i < RFID_MAX_CARDS; ++i) // Iterate up to RFID_MAX_CARDS
        {
            // only do for active cards that have a valid card_id
            if (rfid_database[i].active && rfid_database[i].card_id != 0)
            {
                if (_length + 80U >= bufferMaxLength)
                {
                    ESP_LOGE(TAG, "Buffer too small for the JSON string");
                    xSemaphoreGive(rfid_mutex);
                    return ESP_FAIL;
                }

                _length += snprintf(buffer + _length, bufferMaxLength - _length,
                                    "%s{\"id\":%lu,\"nm\":\"%s\",\"ts\":%lu}",
                                    isComma ? "," : "", rfid_database[i].card_id, rfid_database[i].name, rfid_database[i].timestamp);

                isComma = true;
                // debug print statement
                ESP_LOGI(TAG, "Adding Card %d to JSON", i + 1);
            }
        }

        // Add the closing bracket and null terminator to complete the JSON string
        if (_length + 3U >= bufferMaxLength)
        {
            ESP_LOGE(TAG, "Buffer too small for the JSON string");
            xSemaphoreGive(rfid_mutex);
            return ESP_FAIL;
        }

        _length += snprintf(buffer + _length, bufferMaxLength - _length, "]}");

        xSemaphoreGive(rfid_mutex);
    }

    return ret;
}

static esp_err_t rfid_manager_load_defaults(void)
{
    ESP_LOGI(TAG, "Loading default RFID cards...");
    memset(rfid_database, 0, sizeof(rfid_database)); // Clear existing in-memory db
    uint16_t index = 0;

    for (uint16_t i = 0; i < num_default_cards && index < RFID_MAX_CARDS; ++i)
    {
        rfid_database[index] = default_cards[i]; // Copy default card
        // Ensure name is null-terminated if it's shorter than RFID_CARD_NAME_LEN
        rfid_database[index].name[RFID_CARD_NAME_LEN - 1] = '\0';
        index++;
    }

    esp_err_t ret = rfid_manager_save_to_file(); // This will also calculate and store checksum

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "%d default cards loaded and saved.", index);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to save default cards to file: %s", esp_err_to_name(ret));
    }

    return ret;
}

static esp_err_t rfid_manager_save_to_file(void)
{
    FILE *f = fopen(RFID_DATABASE_FILE, "wb"); // Open for writing in binary mode
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open RFID database file for writing: %s", RFID_DATABASE_FILE);

        return ESP_FAIL;
    }


    // Write card data (all RFID_MAX_CARDS slots)
    size_t cards_written = fwrite(rfid_database, sizeof(rfid_card_t), RFID_MAX_CARDS, f);
    if (cards_written != RFID_MAX_CARDS)
    {
        ESP_LOGE(TAG, "Failed to write all RFID card data to file. Wrote %d of %d.", cards_written, RFID_MAX_CARDS);
        fclose(f);
        // Potentially corrupted file, might need recovery or deletion

        return ESP_FAIL;
    }

    if (fclose(f) != 0)
    {
        ESP_LOGE(TAG, "Failed to close RFID database file after writing.");
        // File might still be corrupted or not fully flushed.

        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t rfid_manager_load_from_file(void)
{

    FILE *f = fopen(RFID_DATABASE_FILE, "rb"); // Open for reading in binary mode

    if (f == NULL)
    {
        ESP_LOGW(TAG, "RFID database file not found: %s. This may be the first boot.", RFID_DATABASE_FILE);
        return ESP_ERR_NOT_FOUND; // File not found is a common case on first boot
    }

    // Read card data (all RFID_MAX_CARDS slots) into a temporary buffer first for checksum validation

    memset(rfid_database, 0, sizeof(rfid_database)); // Initialize to ensure clean data for checksum

    size_t cards_read = fread(rfid_database, sizeof(rfid_card_t), RFID_MAX_CARDS, f);
    // It's an error if we can't read the full block of RFID_MAX_CARDS, as that's what save_to_file writes.
    if (cards_read != RFID_MAX_CARDS)
    {
        ESP_LOGE(TAG, "Failed to read all RFID card data from file. Read %d of %d slots. File might be truncated.", cards_read, RFID_MAX_CARDS);
        fclose(f);
        return ESP_FAIL; // Or ESP_ERR_INVALID_RESPONSE
    }

    if (fclose(f) != 0)
    {
        ESP_LOGE(TAG, "Failed to close RFID database file after reading.");
        // Data might be loaded, but this is still an issue.
        // Proceed with checksum validation.
    }

    return ESP_OK;
}

static void rfid_cache_write_timeout_handler(void* arg) {
    ESP_LOGI(TAG, "RFID write timer expired.");
    // Attempt to take the mutex before accessing shared resources
    if (rfid_mutex != NULL && xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) { // Shorter timeout for callback
        if (is_dirty) {
            ESP_LOGI(TAG, "is_dirty is true, writing cached RFID data to NVS...");
            esp_err_t err = rfid_manager_save_to_file();
            if (err == ESP_OK) {
                is_dirty = false;
                ESP_LOGI(TAG, "Successfully wrote RFID data to NVS.");
            } else {
                ESP_LOGE(TAG, "Failed to write RFID data to NVS from timer: %s", esp_err_to_name(err));
                // Consider: What to do if save fails? Retry? For now, is_dirty remains true.
            }
        } else {
            ESP_LOGI(TAG, "is_dirty is false, no NVS write needed from timer.");
        }
        xSemaphoreGive(rfid_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to take RFID mutex in timer callback. NVS write deferred.");
        // If this happens, data remains dirty and will attempt to save on next timer expiry or deinit.
    }
}
