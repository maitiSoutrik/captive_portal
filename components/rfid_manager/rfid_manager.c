#include "rfid_manager.h"
#include <string.h>
#include "esp_spiffs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h" // For mutex
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

// --- Helper Functions ---

static uint32_t calculate_checksum(const rfid_card_t *cards, uint16_t num_cards_to_checksum) {
    uint32_t checksum = 0;
    // Ensure we don't read past RFID_MAX_CARDS if num_cards_to_checksum is too large,
    // though it should typically be db_header.card_count or the number of cards being written.
    uint16_t count = (num_cards_to_checksum > RFID_MAX_CARDS) ? RFID_MAX_CARDS : num_cards_to_checksum;

    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t *byte_ptr = (const uint8_t *)&cards[i];
        for (size_t j = 0; j < sizeof(rfid_card_t); ++j) {
            checksum += byte_ptr[j]; // Simple sum checksum
            checksum ^= (checksum << 5); // A bit of mixing
        }
    }
    return checksum;
}

// --- Core API Functions ---

esp_err_t rfid_manager_init(void) {
    if (rfid_mutex == NULL) {
        rfid_mutex = xSemaphoreCreateMutex();
        if (rfid_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create RFID mutex");
            return ESP_FAIL;
        }
    }

    if (xSemaphoreTake(rfid_mutex, portMAX_DELAY) == pdTRUE) {
        esp_err_t ret; // Declared once at the beginning of the scope

        // Initialize SPIFFS if not already handled by a dedicated component.
        // The issue mentions `spi_ffs_storage` component. If that component
        // guarantees SPIFFS is mounted, this call might be redundant or
        // could be a check. For now, let's assume it needs to be initialized
        // or ensured here.
        // A better approach would be to have spi_ffs_storage_init() called in main.c
        // and this component just uses the VFS.
        // Check if SPIFFS is mounted. It's assumed to be initialized by another component (e.g. spi_ffs_storage or main app).
        size_t total_bytes, used_bytes;
        esp_err_t spiffs_ret = esp_spiffs_info(NULL, &total_bytes, &used_bytes); // Use default partition label (NULL)
        if (spiffs_ret != ESP_OK) {
            ESP_LOGE(TAG, "SPIFFS filesystem not found or not mounted. Please initialize SPIFFS first. Error: %s", esp_err_to_name(spiffs_ret));
            xSemaphoreGive(rfid_mutex);
            return ESP_ERR_INVALID_STATE; // Indicate that a required pre-condition (SPIFFS mounted) is not met.
        }
        ESP_LOGI(TAG, "SPIFFS filesystem found. Partition size: total: %zu, used: %zu", total_bytes, used_bytes);

        // Attempt to load cards from file
        ret = rfid_manager_load_from_file(); // Assign to the already declared ret

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "RFID database loaded successfully from file. %d cards.", db_header.card_count);
        } else {
            ESP_LOGW(TAG, "Failed to load RFID database from file (%s). Loading defaults.", esp_err_to_name(ret));
            // If loading failed (file not found, corrupted, etc.), load default cards.
            ret = rfid_manager_load_defaults();
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to load default RFID cards. System may not function correctly.");
                // This is a critical failure if defaults can't be loaded.
            } else {
                 ESP_LOGI(TAG, "Default RFID cards loaded and saved.");
            }
        }
        xSemaphoreGive(rfid_mutex);
        return ret; // Return status of load_from_file or load_defaults
    } else {
        ESP_LOGE(TAG, "Failed to take RFID mutex in init");
        return ESP_FAIL;
    }
}

esp_err_t rfid_manager_load_defaults(void) 
{    
    ESP_LOGI(TAG, "Loading default RFID cards...");
    memset(rfid_database, 0, sizeof(rfid_database)); // Clear existing in-memory db
    db_header.card_count = 0; // Reset card count

    for (uint16_t i = 0; i < num_default_cards && db_header.card_count < RFID_MAX_CARDS; ++i) {
        rfid_database[db_header.card_count] = default_cards[i]; // Copy default card
        // Ensure name is null-terminated if it's shorter than RFID_CARD_NAME_LEN
        rfid_database[db_header.card_count].name[RFID_CARD_NAME_LEN - 1] = '\0';
        db_header.card_count++;
    }
    db_header.max_cards = RFID_MAX_CARDS;
    db_header.reserved = 0;
    // Checksum will be calculated by save_to_file

    esp_err_t ret = rfid_manager_save_to_file(); // This will also calculate and store checksum
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "%d default cards loaded and saved.", db_header.card_count);
    } else {
        ESP_LOGE(TAG, "Failed to save default cards to file: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

esp_err_t rfid_manager_save_to_file(void) 
{
    FILE *f = fopen(RFID_DATABASE_FILE, "wb"); // Open for writing in binary mode
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open RFID database file for writing: %s", RFID_DATABASE_FILE);

        return ESP_FAIL;
    }

    db_header.checksum = calculate_checksum(rfid_database, RFID_MAX_CARDS);

    // Write header
    if (fwrite(&db_header, sizeof(rfid_db_header_t), 1, f) != 1) {
        ESP_LOGE(TAG, "Failed to write RFID database header to file.");
        fclose(f);
        
        return ESP_FAIL;
    }

    // Write card data (all RFID_MAX_CARDS slots)
    size_t cards_written = fwrite(rfid_database, sizeof(rfid_card_t), RFID_MAX_CARDS, f);
    if (cards_written != RFID_MAX_CARDS) {
        ESP_LOGE(TAG, "Failed to write all RFID card data to file. Wrote %d of %d.", cards_written, RFID_MAX_CARDS);
        fclose(f);
        // Potentially corrupted file, might need recovery or deletion
        
        return ESP_FAIL;
    }

    if (fclose(f) != 0) {
        ESP_LOGE(TAG, "Failed to close RFID database file after writing.");
        // File might still be corrupted or not fully flushed.
        
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "RFID database saved to file. Header: count=%u, max=%u, checksum=0x%08lx. %d card slots written.",
                db_header.card_count, db_header.max_cards, (unsigned long)db_header.checksum, RFID_MAX_CARDS);

    return ESP_OK;
}

esp_err_t rfid_manager_load_from_file(void) 
{

    FILE *f = fopen(RFID_DATABASE_FILE, "rb"); // Open for reading in binary mode

    if (f == NULL) 
    {
        ESP_LOGW(TAG, "RFID database file not found: %s. This may be the first boot.", RFID_DATABASE_FILE);
        return ESP_ERR_NOT_FOUND; // File not found is a common case on first boot
    }

    // Read header
    rfid_db_header_t temp_header;

    if (fread(&temp_header, sizeof(rfid_db_header_t), 1, f) != 1) {
        ESP_LOGE(TAG, "Failed to read RFID database header from file.");
        fclose(f);
        return ESP_FAIL; // Or ESP_ERR_INVALID_RESPONSE if file is too short
    }

    // Basic validation of header values before proceeding
    if (temp_header.max_cards != RFID_MAX_CARDS) {
        ESP_LOGE(TAG, "RFID database header mismatch: max_cards in file (%u) != expected (%u).",
                    temp_header.max_cards, RFID_MAX_CARDS);
        fclose(f);
        return ESP_ERR_INVALID_STATE; // Or a custom error for header corruption
    }
    // card_count can be 0 up to max_cards
    if (temp_header.card_count > RFID_MAX_CARDS) {
            ESP_LOGE(TAG, "RFID database header corruption: card_count (%u) > max_cards (%u).",
                    temp_header.card_count, RFID_MAX_CARDS);
        fclose(f);
        return ESP_ERR_INVALID_STATE;
    }


    // Read card data (all RFID_MAX_CARDS slots) into a temporary buffer first for checksum validation
    
    memset(temp_rfid_database, 0, sizeof(temp_rfid_database)); // Initialize to ensure clean data for checksum

    size_t cards_read = fread(temp_rfid_database, sizeof(rfid_card_t), RFID_MAX_CARDS, f);
    // It's an error if we can't read the full block of RFID_MAX_CARDS, as that's what save_to_file writes.
    if (cards_read != RFID_MAX_CARDS) {
        ESP_LOGE(TAG, "Failed to read all RFID card data from file. Read %d of %d slots. File might be truncated.", cards_read, RFID_MAX_CARDS);
        fclose(f);
        return ESP_FAIL; // Or ESP_ERR_INVALID_RESPONSE
    }

    if (fclose(f) != 0) {
        ESP_LOGE(TAG, "Failed to close RFID database file after reading.");
        // Data might be loaded, but this is still an issue.
        // Proceed with checksum validation.
    }

    // Verify checksum
    uint32_t calculated_checksum = calculate_checksum(temp_rfid_database, RFID_MAX_CARDS);
    if (calculated_checksum != temp_header.checksum) {
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


// --- Stubs for other functions to be implemented ---

esp_err_t rfid_manager_add_card(uint32_t card_id, const char* name) {
    if (xSemaphoreTake(rfid_mutex, portMAX_DELAY) == pdTRUE) {
        if (name == NULL) {
            ESP_LOGE(TAG, "Cannot add card with NULL name.");
            xSemaphoreGive(rfid_mutex);
            return ESP_ERR_INVALID_ARG;
        }

        // Check if card already exists by iterating through all possible slots
        for (uint16_t i = 0; i < RFID_MAX_CARDS; ++i) {
            if (rfid_database[i].card_id == card_id) { // Check if card_id matches
                ESP_LOGI(TAG, "Card 0x%08lx already exists at slot %u. Updating name and ensuring active.", (unsigned long)card_id, i);
                strncpy(rfid_database[i].name, name, RFID_CARD_NAME_LEN - 1);
                rfid_database[i].name[RFID_CARD_NAME_LEN -1] = '\0'; // Ensure null termination
                rfid_database[i].active = 1;
                rfid_database[i].timestamp = 0; // Or update timestamp if needed
                esp_err_t save_ret = rfid_manager_save_to_file();
                xSemaphoreGive(rfid_mutex);
                return save_ret;
            }
        }

        // If card does not exist, try to add to the first inactive slot or at the end
        uint16_t slot_to_add = RFID_MAX_CARDS;

        // First, look for an inactive slot to reuse
        for(uint16_t i=0; i < RFID_MAX_CARDS; ++i) {
            // This logic assumes that card_id == 0 can mean an empty/never-used slot,
            // or we rely on db_header.card_count to know the "end" of active cards
            // if we compact the array on removal.
            // The current design saves all RFID_MAX_CARDS slots.
            // An inactive card would have card_id != 0 but active == 0.
            // A truly empty slot might have card_id == 0.
            // Let's find the first slot where card_id is 0 (never used) or active is 0 (previously removed)
            if (rfid_database[i].card_id == 0 || rfid_database[i].active == 0) {
                slot_to_add = i;
                break;
            }
        }


        if (slot_to_add < RFID_MAX_CARDS) {
            rfid_database[slot_to_add].card_id = card_id;
            strncpy(rfid_database[slot_to_add].name, name, RFID_CARD_NAME_LEN - 1);
            rfid_database[slot_to_add].name[RFID_CARD_NAME_LEN - 1] = '\0';
            rfid_database[slot_to_add].active = 1;
            rfid_database[slot_to_add].timestamp = 0; // Set current timestamp if needed

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
            for(uint16_t i=0; i < RFID_MAX_CARDS; ++i) {
                if(rfid_database[i].active && rfid_database[i].card_id != 0) {
                    active_count++;
                }
            }
            if (active_count > RFID_MAX_CARDS) { // Should not happen if slot_to_add was valid
                 ESP_LOGE(TAG, "Card count exceeds max cards after add. This is a bug.");
                 xSemaphoreGive(rfid_mutex);
                 return ESP_FAIL; // Should not happen
            }
            if (db_header.card_count >= RFID_MAX_CARDS && slot_to_add >= db_header.card_count) {
                 // This check is tricky. If all RFID_MAX_CARDS slots are filled with *active* cards,
                 // then we are full. If some are inactive, we can reuse.
                 // The previous loop for slot_to_add should find an inactive one if available.
                 // If slot_to_add is still RFID_MAX_CARDS, it means all slots are used by *something*.
                 // We need to check if all *active* cards fill the capacity.
                 if (active_count >= RFID_MAX_CARDS && rfid_database[slot_to_add].card_id != 0 && rfid_database[slot_to_add].active == 1) {
                    ESP_LOGW(TAG, "RFID database is full. Cannot add new card 0x%08lx.", (unsigned long)card_id);
                    xSemaphoreGive(rfid_mutex);
                    return ESP_ERR_NO_MEM;
                 }
            }


            db_header.card_count = active_count; // Update active card count

            ESP_LOGI(TAG, "Added card 0x%08lx ('%s') at slot %u. Active cards: %u", (unsigned long)card_id, name, slot_to_add, db_header.card_count);
            esp_err_t save_ret = rfid_manager_save_to_file();
            xSemaphoreGive(rfid_mutex);
            return save_ret;
        } else {
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


esp_err_t rfid_manager_remove_card(uint32_t card_id) {
    if (xSemaphoreTake(rfid_mutex, portMAX_DELAY) == pdTRUE) {
        for (uint16_t i = 0; i < RFID_MAX_CARDS; ++i) { // Iterate all possible slots
            if (rfid_database[i].card_id == card_id && rfid_database[i].active) {
                rfid_database[i].active = 0; // Mark as inactive
                // Optionally clear name and timestamp
                // memset(rfid_database[i].name, 0, RFID_CARD_NAME_LEN);
                // rfid_database[i].timestamp = 0;

                // Recalculate active card count
                uint16_t active_count = 0;
                for(uint16_t j=0; j < RFID_MAX_CARDS; ++j) {
                    if(rfid_database[j].active && rfid_database[j].card_id != 0) {
                        active_count++;
                    }
                }
                db_header.card_count = active_count;

                ESP_LOGI(TAG, "Removed card 0x%08lx. Active cards: %u", (unsigned long)card_id, db_header.card_count);
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

bool rfid_manager_check_card(uint32_t card_id) {
    if (xSemaphoreTake(rfid_mutex, portMAX_DELAY) == pdTRUE) {
        for (uint16_t i = 0; i < RFID_MAX_CARDS; ++i) { // Iterate all slots
            if (rfid_database[i].card_id == card_id && rfid_database[i].active) {
                // Optionally update timestamp here if tracking last access
                // rfid_database[i].timestamp = esp_log_timestamp(); // Or time(NULL)
                // rfid_manager_save_to_file(); // If timestamp is updated
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

uint16_t rfid_manager_get_card_count(void) {
    // This can return the stored db_header.card_count if it's reliably updated.
    // Taking mutex for consistency, though it might be okay for a quick read if not critical.
    uint16_t count = 0;
    if (xSemaphoreTake(rfid_mutex, portMAX_DELAY) == pdTRUE) {
        count = db_header.card_count;
        xSemaphoreGive(rfid_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to take RFID mutex in get_card_count");
        // Return a safe value or last known value.
    }
    return count;
}

esp_err_t rfid_manager_list_cards(rfid_card_t* cards_buffer, uint16_t buffer_size, uint16_t* num_cards_copied) {
    if (cards_buffer == NULL || num_cards_copied == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(rfid_mutex, portMAX_DELAY) == pdTRUE) {
        uint16_t active_cards_found = 0;
        for (uint16_t i = 0; i < RFID_MAX_CARDS && active_cards_found < buffer_size; ++i) {
            if (rfid_database[i].active && rfid_database[i].card_id != 0) {
                cards_buffer[active_cards_found] = rfid_database[i];
                active_cards_found++;
            }
        }
        *num_cards_copied = active_cards_found;
        xSemaphoreGive(rfid_mutex);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Failed to take RFID mutex in list_cards");
    *num_cards_copied = 0;
    return ESP_FAIL;
}

esp_err_t rfid_manager_format_database(void) {
    if (xSemaphoreTake(rfid_mutex, portMAX_DELAY) == pdTRUE) {
        ESP_LOGW(TAG, "Formatting RFID database. All existing cards will be erased and defaults loaded.");
        // Essentially, just load defaults, which will overwrite the file.
        esp_err_t ret = rfid_manager_load_defaults();
        xSemaphoreGive(rfid_mutex);
        return ret;
    }
    ESP_LOGE(TAG, "Failed to take RFID mutex in format_database");
    return ESP_FAIL;
}

// This function might be mostly for internal use by init, or for diagnostics.
// The primary check is the checksum during load_from_file.
bool rfid_manager_is_database_valid(void) {
    // A simple check could be if the mutex is initialized and db_header.max_cards is correct.
    // A more thorough check would re-calculate and compare checksum of in-memory data,
    // but that's already done during load.
    bool is_valid = false;
    if (xSemaphoreTake(rfid_mutex, portMAX_DELAY) == pdTRUE) {
        // If db_header.max_cards is not set to RFID_MAX_CARDS, it implies it was never loaded correctly.
        if (db_header.max_cards == RFID_MAX_CARDS) {
            // Could also re-calculate checksum of in-memory rfid_database and compare with db_header.checksum
            // uint32_t current_checksum = calculate_checksum(rfid_database, RFID_MAX_CARDS);
            // if (current_checksum == db_header.checksum) {
            //    is_valid = true;
            // }
            // For now, if max_cards is correct, assume it's somewhat valid post-init.
            is_valid = true;
        }
        xSemaphoreGive(rfid_mutex);
    } else {
         ESP_LOGE(TAG, "Failed to take RFID mutex in is_database_valid");
    }
    return is_valid;
}
