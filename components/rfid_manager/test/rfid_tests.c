/* test_mean.c: Implementation of a testable component.

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <limits.h>
#include "unity.h"
#include "rfid_manager.h"
#include <string.h>
#include <stdio.h> // For FILE operations (fopen, fwrite, fclose) for corruption test

#define countof(x) (sizeof(x) / sizeof(x[0]))
// Define a known number of default cards for assertion, based on rfid_manager.c
// This is a bit fragile if default_cards array changes size without updating test.
// A better way would be to list cards and check for specific default cards.
#define NUM_DEFAULT_CARDS 3

TEST_CASE("RFID Manager: INIT", "[rfid_manager]")
{
    esp_err_t ret = rfid_manager_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

TEST_CASE("RFID Manager: Adding Card", "[rfid_manager]")
{
    // Ensure a clean state by formatting the database (loads defaults)
    esp_err_t format_ret = rfid_manager_format_database();
    TEST_ASSERT_EQUAL(ESP_OK, format_ret);

    // Attempt to add a new card with a unique ID
    esp_err_t ret = rfid_manager_add_card(0xABCDEFFF, "New Unique Card");
    TEST_ASSERT_EQUAL(ESP_OK, ret); // This should succeed

    // Attempt to add a card that already exists (default card ID 0x12345678)
    // This should now fail with ESP_ERR_INVALID_STATE due to the fix
    ret = rfid_manager_add_card(0x12345678, "Attempt to overwrite");
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);

    // Attempt to add another new card to ensure the database is still functional
    esp_err_t ret2 = rfid_manager_add_card(0x11223344, "Another New Card");
    TEST_ASSERT_EQUAL(ESP_OK, ret2);
}

TEST_CASE("RFID Manager: Getting Card", "[rfid_manager]")
{
    // Ensure a clean state by formatting the database for this test too,
    // so we know what to expect for "Admin Card" (0x12345678)
    esp_err_t format_ret = rfid_manager_format_database();
    TEST_ASSERT_EQUAL(ESP_OK, format_ret);

    // Check if the card was added correctly
    rfid_card_t card;
    esp_err_t ret = rfid_manager_get_card(0x12345678, &card);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    TEST_ASSERT_EQUAL_UINT32(0x12345678, card.card_id);
    TEST_ASSERT_EQUAL_STRING("Admin Card", card.name); // Corrected expected name

    TEST_ASSERT_EQUAL_UINT8(1, card.active);
}

TEST_CASE("RFID Manager: Fill Database (Performance/Stress)", "[rfid_manager]")
{
    esp_err_t ret;

    // Ensure a clean state by formatting the database (loads defaults)
    ret = rfid_manager_format_database();
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    uint16_t initial_card_count = rfid_manager_get_card_count();

    char card_name[RFID_CARD_NAME_LEN];
    uint32_t base_card_id = 0x20000000; // Base ID for new cards to avoid collision with defaults

    for (uint16_t i = 0; i < (RFID_MAX_CARDS - initial_card_count); ++i)
    {
        uint32_t current_card_id = base_card_id + i;
        snprintf(card_name, RFID_CARD_NAME_LEN, "StressCard %u", i);
        
        ret = rfid_manager_add_card(current_card_id, card_name);
        // Removed ESP_LOGE for brevity in tests, focusing on assertion
        TEST_ASSERT_EQUAL(ESP_OK, ret);
    }

    uint16_t final_card_count = rfid_manager_get_card_count();
    TEST_ASSERT_EQUAL_UINT16(RFID_MAX_CARDS, final_card_count);

    // Attempt to add one more card, should fail with ESP_ERR_NO_MEM
    ret = rfid_manager_add_card(base_card_id + RFID_MAX_CARDS, "Overflow Card");
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, ret);
}

TEST_CASE("RFID Manager: File Corruption and Recovery", "[rfid_manager]")
{
    esp_err_t ret;

    // 1. Initial setup: Initialize and create a known (non-default) file state
    // Ensure SPIFFS is up and rfid_manager can run.
    // The first rfid_manager_init() might have run from a previous test if tests share state,
    // or if this is the first test in a fresh run.
    // Formatting ensures we start from a known point (defaults).
    ret = rfid_manager_format_database(); 
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    // Add a custom card to make the current rfid_cards.dat distinct from the final default state
    uint32_t custom_card_id = 0xDDCCBBAA;
    const char* custom_card_name = "CustomCorruptTest";
    ret = rfid_manager_add_card(custom_card_id, custom_card_name);
    TEST_ASSERT_EQUAL(ESP_OK, ret); // This saves the file with the custom card

    // Verify custom card exists and count is NUM_DEFAULT_CARDS + 1
    rfid_card_t temp_card;
    ret = rfid_manager_get_card(custom_card_id, &temp_card);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_STRING(custom_card_name, temp_card.name);
    TEST_ASSERT_EQUAL_UINT16(NUM_DEFAULT_CARDS + 1, rfid_manager_get_card_count());

    // 2. Programmatically corrupt the rfid_cards.dat file
    const char* filepath = "/spiffs/rfid_cards.dat";
    FILE* f = fopen(filepath, "wb"); // Open in write binary, truncates/overwrites the file
    TEST_ASSERT_NOT_NULL(f);         // Ensure file opened successfully
    
    if (f) {
        char garbage_data[] = "corrupted_file_data_to_trigger_error";
        size_t written_count = fwrite(garbage_data, 1, sizeof(garbage_data) - 1, f);
        TEST_ASSERT_EQUAL_UINT(sizeof(garbage_data) - 1, written_count);
        int fclose_ret = fclose(f);
        TEST_ASSERT_EQUAL(0, fclose_ret);
    } else {
        TEST_FAIL_MESSAGE("Failed to open rfid_cards.dat for corruption part of the test.");
    }

    // 3. Re-initialize the RFID manager.
    // This call to rfid_manager_init() should detect the corruption (e.g. checksum mismatch or parse error)
    // and then call rfid_manager_load_defaults().
    // The rfid_manager_init() itself should return ESP_OK if recovery by loading defaults is successful.
    ret = rfid_manager_init(); 
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    // 4. Verify recovery to default state
    // Check that the custom card is no longer found
    ret = rfid_manager_get_card(custom_card_id, &temp_card);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, ret);

    // Check that a known default card IS present (e.g., 0x12345678 "Admin Card")
    uint32_t default_admin_card_id = 0x12345678; // Assuming this is a default card
    ret = rfid_manager_get_card(default_admin_card_id, &temp_card);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_STRING("Admin Card", temp_card.name); // Assuming this is its name

    // Verify the card count is back to the number of default cards
    TEST_ASSERT_EQUAL_UINT16(NUM_DEFAULT_CARDS, rfid_manager_get_card_count());
}
