#ifndef RFID_MANAGER_H
#define RFID_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define RFID_MAX_CARDS 200
#define RFID_CARD_NAME_LEN 32

// RFID card structure
// Size: 4 (card_id) + 1 (active) + 32 (name) + 4 (timestamp) = 41 bytes
typedef struct {
    uint32_t card_id;     // 32-bit RFID card number
    uint8_t active;       // Card status (1=active, 0=inactive/removed)
    char name[RFID_CARD_NAME_LEN]; // Card holder name (ensure null-termination if shorter)
    uint32_t timestamp;   // Last access timestamp (0 if not used)
} rfid_card_t;

/**
 * @brief Initializes the RFID manager.
 *
 * This function should be called once at startup. It will:
 * 1. Ensure the SPIFFS filesystem is mounted (usually handled by spi_ffs_storage component).
 * 2. Attempt to load RFID cards from the persistent storage file.
 * 3. If the file doesn't exist, is empty, or corrupted (checksum mismatch),
 *    it will initialize the database with default cards.
 *
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t rfid_manager_init(void);

/**
 * @brief Adds a new RFID card to the database.
 *
 * Adds the given card_id and name to the list of authorized cards.
 * If a card with the same card_id already exists (either active or inactive),
 * this function will return an error and will not modify the existing entry.
 * If the database is full, an error will be returned.
 * Changes are persisted to the storage file if a new card is successfully added.
 *
 * @param card_id The 32-bit ID of the RFID card to add. Must not be 0.
 * @param name Pointer to a string containing the name associated with the card. Must not be NULL.
 *             The name will be truncated if longer than RFID_CARD_NAME_LEN-1.
 * @return esp_err_t ESP_OK on success.
 *         ESP_ERR_INVALID_ARG if name is NULL or card_id is 0 (if 0 is considered invalid).
 *         ESP_ERR_INVALID_STATE if the card_id already exists in the database.
 *         ESP_ERR_NO_MEM if the database is full.
 *         ESP_FAIL for other file operation errors.
 */
esp_err_t rfid_manager_add_card(uint32_t card_id, const char* name);

/**
 * @brief Removes an RFID card from the database.
 *
 * Marks the card with the given card_id as inactive.
 * The card data might remain in the file but will be treated as invalid.
 * Changes are persisted to the storage file.
 *
 * @param card_id The 32-bit ID of the RFID card to remove.
 * @return esp_err_t ESP_OK if the card was found and marked inactive,
 *         ESP_ERR_NOT_FOUND if the card_id does not exist,
 *         ESP_FAIL for other errors.
 */
esp_err_t rfid_manager_remove_card(uint32_t card_id);

/**
 * @brief Retrieves an RFID card by its ID.
 *
 * Fetches the card details for the given card_id.
 * If the card is not found or inactive, it will return an error.
 *
 * @param card_id The 32-bit ID of the RFID card to retrieve.
 * @param card Pointer to a rfid_card_t structure where the card data will be stored.
 * @return esp_err_t ESP_OK on success, ESP_ERR_NOT_FOUND if the card does not exist,
 *         ESP_ERR_INVALID_ARG if card is NULL, ESP_FAIL for other errors.
 */
esp_err_t rfid_manager_get_card(uint32_t card_id, rfid_card_t *card);

/**
 * @brief Checks if an RFID card is authorized.
 *
 * Verifies if the given card_id exists in the database and is marked as active.
 * If the card is found and active, its timestamp is updated in the in-memory database.
 * NOTE: This timestamp update is NOT immediately persisted to the file system
 * to reduce flash wear and improve performance. It will be persisted upon the next
 * explicit save operation (e.g., when adding or removing a card).
 *
 * @param card_id The 32-bit ID of the RFID card to check.
 * @return true if the card is authorized and active, false otherwise.
 */
bool rfid_manager_check_card(uint32_t card_id);

/**
 * @brief Gets the total number of active RFID cards in the database.
 *
 * @return uint16_t The count of currently active RFID cards.
 */
uint16_t rfid_manager_get_card_count(void);

/**
 * @brief Retrieves a list of all active RFID cards.
 *
 * Populates the provided 'cards_buffer' with data of all active RFID cards.
 *
 * @param cards_buffer Pointer to an array of rfid_card_t structures to store the card data.
 * @param buffer_size The maximum number of rfid_card_t elements the cards_buffer can hold.
 *                    It's recommended this be at least RFID_MAX_CARDS.
 * @param num_cards_copied Pointer to a uint16_t that will be filled with the number of cards actually copied to the buffer.
 * @return esp_err_t ESP_OK on success, ESP_ERR_INVALID_ARG if buffer is NULL or num_cards_copied is NULL.
 */
esp_err_t rfid_manager_list_cards(rfid_card_t *cards_buffer, uint16_t buffer_size, uint16_t *num_cards_copied);

/**
 * @brief Formats the RFID database.
 *
 * Clears all existing RFID cards from the database and re-initializes it
 * by loading the default set of cards.
 * This operation is destructive to existing card data.
 *
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t rfid_manager_format_database(void);


esp_err_t rfid_manager_get_card_list_json(char* buffer, size_t bufferLength);

/**
 * @brief Sets the cache write timeout for testing purposes.
 *
 * Allows unit tests to use a shorter timeout for the RFID cache write-back timer.
 * Should typically be called in the test's setUp function.
 *
 * @param timeout_ms The timeout in milliseconds. If 0, it might reset to default (optional behavior).
 */
void rfid_manager_set_cache_write_timeout(uint32_t timeout_ms);


#endif // RFID_MANAGER_H
