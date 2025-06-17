#include "rfid_manager.h"
#include <string.h>
#include "esp_spiffs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h" // For mutex
#include <time.h>            // For time()
// #include <inttypes.h> // PRIX32 not used, using %lx with cast instead

static const char *TAG = "RFID_MANAGER";

#define RFID_DATABASE_FILE "/spiffs/rfid_cards.dat"

// In-memory database for RFID cards
static rfid_card_t rfid_database[RFID_MAX_CARDS];

static rfid_card_t temp_rfid_database[RFID_MAX_CARDS];

// Header for the in-memory database (mirrors what's in the file)
static rfid_db_header_t db_header;

// Mutex for thread-safe access to the database and file operations
static SemaphoreHandle_t rfid_mutex = NULL;

// Default RFID cards
static const rfid_card_t default_cards[] = {
    {0x12345678, 1, "Admin Card", 0},
    {0x87654321, 1, "User Card 1", 0},
    {0xABCDEF00, 1, "User Card 2", 0}
    // Add more default cards if needed, up to RFID_MAX_CARDS
};
static const uint16_t num_default_cards = sizeof(default_cards) / sizeof(rfid_card_t);

// Forward declarations for internal helper functions
static uint32_t calculate_checksum(const rfid_card_t *cards, uint16_t num_cards);
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
 * @brief Checks if the loaded database is valid.
 *
 * This is typically an internal check performed after loading from file,
 * primarily by verifying the checksum.
 *
 * @return true if the database is considered valid, false otherwise.
 */
static bool rfid_manager_is_database_valid(void); // May not be needed as public API if init handles it.

// --- Helper Functions ---

static uint32_t calculate_checksum(const rfid_card_t *cards, uint16_t num_cards_to_checksum)
{
    uint32_t checksum = 0;
    // Ensure we don't read past RFID_MAX_CARDS if num_cards_to_checksum is too large,
    // though it should typically be db_header.card_count or the number of cards being written.
    uint16_t count = (num_cards_to_checksum > RFID_MAX_CARDS) ? RFID_MAX_CARDS : num_cards_to_checksum;

    for (uint16_t i = 0; i < count; ++i)
    {
        const uint8_t *byte_ptr = (const uint8_t *)&cards[i];
        for (size_t j = 0; j < sizeof(rfid_card_t); ++j)
        {
            checksum += byte_ptr[j];     // Simple sum checksum
            checksum ^= (checksum << 5); // A bit of mixing
        }
    }
    return checksum;
}

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

    if (xSemaphoreTake(rfid_mutex, portMAX_DELAY) == pdTRUE)
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
            ESP_LOGI(TAG, "RFID database loaded successfully from file. %d cards.", db_header.card_count);
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

        xSemaphoreGive(rfid_mutex);
        return ret; // Return status of load_from_file or load_defaults
    }
    else
    {
        ESP_LOGE(TAG, "Failed to take RFID mutex in init");
        return ESP_FAIL;
    }
}

esp_err_t rfid_manager_add_card(uint32_t card_id, const char *name)
{
    if (xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
    {
        if (name == NULL)
        {
            ESP_LOGE(TAG, "Cannot add card with NULL name.");
            xSemaphoreGive(rfid_mutex);
            return ESP_ERR_INVALID_ARG;
        }

        // Check if card already exists by iterating through all possible slots
        for (uint16_t i = 0; i < RFID_MAX_CARDS; ++i)
        {
            if (rfid_database[i].card_id == card_id)
            { // Check if card_id matches
                ESP_LOGI(TAG, "Card 0x%08lx already exists at slot %u. Updating name and ensuring active.", (unsigned long)card_id, i);
                strncpy(rfid_database[i].name, name, RFID_CARD_NAME_LEN - 1);
                rfid_database[i].name[RFID_CARD_NAME_LEN - 1] = '\0'; // Ensure null termination
                rfid_database[i].active = 1;
                time_t now_update;
                time(&now_update);
                rfid_database[i].timestamp = (uint32_t)now_update; // Set current timestamp
                esp_err_t save_ret = rfid_manager_save_to_file();
                xSemaphoreGive(rfid_mutex);
                return save_ret;
            }
        }

        // If card does not exist, try to add to the first inactive slot or at the end
        uint16_t slot_to_add = RFID_MAX_CARDS;

        // First, look for an inactive slot to reuse
        for (uint16_t i = 0; i < RFID_MAX_CARDS; ++i)
        {
            // This logic assumes that card_id == 0 can mean an empty/never-used slot,
            // or we rely on db_header.card_count to know the "end" of active cards
            // if we compact the array on removal.
            // The current design saves all RFID_MAX_CARDS slots.
            // An inactive card would have card_id != 0 but active == 0.
            // A truly empty slot might have card_id == 0.
            // Let's find the first slot where card_id is 0 (never used) or active is 0 (previously removed)
            if (rfid_database[i].card_id == 0 || rfid_database[i].active == 0)
            {
                slot_to_add = i;
                break;
            }
        }

        if (slot_to_add < RFID_MAX_CARDS)
        {
            rfid_database[slot_to_add].card_id = card_id;
            strncpy(rfid_database[slot_to_add].name, name, RFID_CARD_NAME_LEN - 1);
            rfid_database[slot_to_add].name[RFID_CARD_NAME_LEN - 1] = '\0';
            rfid_database[slot_to_add].active = 1;
            time_t now_add;
            time(&now_add);
            rfid_database[slot_to_add].timestamp = (uint32_t)now_add; // Set current timestamp

            // Increment card_count only if we are adding to a slot that was previously "empty"
            // in terms of the active card count. If we are reactivating an inactive card,
            // card_count might not need to change if it only tracks truly active cards.
            // The current db_header.card_count tracks active cards.
            // If we are reusing a slot that was inactive (active=0), and it was part of the original count,
            // then card_count doesn't change. If it was a slot beyond the original card_count, it increases.
            // This logic needs refinement based on how card_count is managed.
            // For now, let's assume card_count is the number of cards with active=1.
            // We need to recount active cards or adjust.
            // A simpler way: if we add a new card, and it's a new active card, increment.
            // If we are reactivating, it becomes active.
            // Let's recalculate active card count.
            uint16_t active_count = 0;
            for (uint16_t i = 0; i < RFID_MAX_CARDS; ++i)
            {
                if (rfid_database[i].active && rfid_database[i].card_id != 0)
                {
                    active_count++;
                }
            }
            if (active_count > RFID_MAX_CARDS)
            { // Should not happen if slot_to_add was valid
                ESP_LOGE(TAG, "Card count exceeds max cards after add. This is a bug.");
                xSemaphoreGive(rfid_mutex);
                return ESP_FAIL; // Should not happen
            }
            if (db_header.card_count >= RFID_MAX_CARDS && slot_to_add >= db_header.card_count)
            {
                // This check is tricky. If all RFID_MAX_CARDS slots are filled with *active* cards,
                // then we are full. If some are inactive, we can reuse.
                // The previous loop for slot_to_add should find an inactive one if available.
                // If slot_to_add is still RFID_MAX_CARDS, it means all slots are used by *something*.
                // We need to check if all *active* cards fill the capacity.
                if (active_count >= RFID_MAX_CARDS && rfid_database[slot_to_add].card_id != 0 && rfid_database[slot_to_add].active == 1)
                {
                    ESP_LOGW(TAG, "RFID database is full. Cannot add new card 0x%08lx.", (unsigned long)card_id);
                    xSemaphoreGive(rfid_mutex);
                    return ESP_ERR_NO_MEM;
                }
            }

            db_header.card_count = active_count; // Update active card count

            ESP_LOGI(TAG, "Added card %lu ('%s') at slot %u. Active cards: %u", (unsigned long)card_id, name, slot_to_add, db_header.card_count);
            esp_err_t save_ret = rfid_manager_save_to_file();
            xSemaphoreGive(rfid_mutex);
            return save_ret;
        }
        else
        {
            // This case (slot_to_add == RFID_MAX_CARDS) means all RFID_MAX_CARDS slots
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
    if (xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
    {
        for (uint16_t i = 0; i < RFID_MAX_CARDS; ++i)
        { // Iterate all possible slots
            if (rfid_database[i].card_id == card_id && rfid_database[i].active)
            {
                rfid_database[i].active = 0; // Mark as inactive
                // Optionally clear name and timestamp
                // memset(rfid_database[i].name, 0, RFID_CARD_NAME_LEN);
                // rfid_database[i].timestamp = 0;

                // Recalculate active card count
                uint16_t active_count = 0;
                for (uint16_t j = 0; j < RFID_MAX_CARDS; ++j)
                {
                    if (rfid_database[j].active && rfid_database[j].card_id != 0)
                    {
                        active_count++;
                    }
                }
                db_header.card_count = active_count;

                ESP_LOGI(TAG, "Removed card %lu. Active cards: %u", (unsigned long)card_id, db_header.card_count);
                esp_err_t save_ret = rfid_manager_save_to_file();
                xSemaphoreGive(rfid_mutex);
                return save_ret;
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
    if (xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
    {
        for (uint16_t i = 0; i < RFID_MAX_CARDS; ++i)
        { // Iterate all slots
            if (rfid_database[i].card_id == card_id && rfid_database[i].active)
            {
                // Update timestamp on successful check
                time_t now;
                time(&now);
                rfid_database[i].timestamp = (uint32_t)now;
                ESP_LOGI(TAG, "Card %lu checked successfully. Timestamp updated to %lu.", (unsigned long)card_id, (unsigned long)rfid_database[i].timestamp);
                
                // Save the updated database to file
                // WARNING: Frequent writes can wear out flash. Consider optimization for production.
                esp_err_t save_err = rfid_manager_save_to_file();
                if (save_err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to save database after timestamp update for card 0x%08lx.", (unsigned long)card_id);
                    // Continue, but the timestamp update might not persist
                }
                
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
    return ESP_FAIL;
}

uint16_t rfid_manager_get_card_count(void)
{
    // This can return the stored db_header.card_count if it's reliably updated.
    // Taking mutex for consistency, though it might be okay for a quick read if not critical.
    uint16_t active_count = 0;
    if (xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
    {
        // Recalculate on demand to ensure accuracy
        for (uint16_t i = 0; i < RFID_MAX_CARDS; ++i)
        {
            if (rfid_database[i].active && rfid_database[i].card_id != 0)
            {
                active_count++;
            }
        }
        // Optionally, update db_header.card_count here if it was found to be out of sync,
        // though add/remove should be keeping it correct.
        // For now, just return the live calculated count.
        // If db_header.card_count was critical for other logic, this might be a place to sync it.
        // ESP_LOGD(TAG, "Recalculated active card count: %u (db_header.card_count was %u)", active_count, db_header.card_count);
        // db_header.card_count = active_count; // If we decide to re-sync the header value
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
    if (xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
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
    esp_err_t ret = ESP_FAIL;
    if (xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(5000)) == pdTRUE) // Longer timeout for format
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

    if (xSemaphoreTake(rfid_mutex, pdMS_TO_TICKS(500)) == pdTRUE) // Longer timeout for format
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
    db_header.card_count = 0;                        // Reset card count

    for (uint16_t i = 0; i < num_default_cards && db_header.card_count < RFID_MAX_CARDS; ++i)
    {
        rfid_database[db_header.card_count] = default_cards[i]; // Copy default card
        // Ensure name is null-terminated if it's shorter than RFID_CARD_NAME_LEN
        rfid_database[db_header.card_count].name[RFID_CARD_NAME_LEN - 1] = '\0';
        db_header.card_count++;
    }
    db_header.max_cards = RFID_MAX_CARDS;
    db_header.reserved = 0;
    // Checksum will be calculated by save_to_file

    esp_err_t ret = rfid_manager_save_to_file(); // This will also calculate and store checksum
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "%d default cards loaded and saved.", db_header.card_count);
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

    db_header.checksum = calculate_checksum(rfid_database, RFID_MAX_CARDS);

    // Write header
    if (fwrite(&db_header, sizeof(rfid_db_header_t), 1, f) != 1)
    {
        ESP_LOGE(TAG, "Failed to write RFID database header to file.");
        fclose(f);

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

    ESP_LOGI(TAG, "RFID database saved to file. Header: count=%u, max=%u, checksum=0x%08lx. %d card slots written.",
             db_header.card_count, db_header.max_cards, (unsigned long)db_header.checksum, RFID_MAX_CARDS);

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

    // Read header
    rfid_db_header_t temp_header;

    if (fread(&temp_header, sizeof(rfid_db_header_t), 1, f) != 1)
    {
        ESP_LOGE(TAG, "Failed to read RFID database header from file.");
        fclose(f);
        return ESP_FAIL; // Or ESP_ERR_INVALID_RESPONSE if file is too short
    }

    // Basic validation of header values before proceeding
    if (temp_header.max_cards != RFID_MAX_CARDS)
    {
        ESP_LOGE(TAG, "RFID database header mismatch: max_cards in file (%u) != expected (%u).",
                 temp_header.max_cards, RFID_MAX_CARDS);
        fclose(f);
        return ESP_ERR_INVALID_STATE; // Or a custom error for header corruption
    }
    // card_count can be 0 up to max_cards
    if (temp_header.card_count > RFID_MAX_CARDS)
    {
        ESP_LOGE(TAG, "RFID database header corruption: card_count (%u) > max_cards (%u).",
                 temp_header.card_count, RFID_MAX_CARDS);
        fclose(f);
        return ESP_ERR_INVALID_STATE;
    }

    // Read card data (all RFID_MAX_CARDS slots) into a temporary buffer first for checksum validation

    memset(temp_rfid_database, 0, sizeof(temp_rfid_database)); // Initialize to ensure clean data for checksum

    size_t cards_read = fread(temp_rfid_database, sizeof(rfid_card_t), RFID_MAX_CARDS, f);
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

    // Verify checksum
    uint32_t calculated_checksum = calculate_checksum(temp_rfid_database, RFID_MAX_CARDS);
    if (calculated_checksum != temp_header.checksum)
    {
        ESP_LOGE(TAG, "RFID database checksum mismatch! File: 0x%08lx, Calculated: 0x%08lx. Database may be corrupt.",
                 (unsigned long)temp_header.checksum, (unsigned long)calculated_checksum);
        return ESP_ERR_INVALID_CRC;
    }

    // Checksum is valid, copy data to the global in-memory database
    memcpy(rfid_database, temp_rfid_database, sizeof(rfid_database));
    db_header = temp_header; // Copy the validated header

    ESP_LOGI(TAG, "RFID database loaded from file. Header: count=%u, max=%u, checksum=0x%08lx. %d card slots read.",
             db_header.card_count, db_header.max_cards, (unsigned long)db_header.checksum, RFID_MAX_CARDS);

    return ESP_OK;
}

// This function might be mostly for internal use by init, or for diagnostics.
// The primary check is the checksum during load_from_file.
static bool rfid_manager_is_database_valid(void)
{
    bool is_valid = false;

    if (db_header.max_cards == RFID_MAX_CARDS)
    {

        is_valid = true;
    }

    return is_valid;
}
