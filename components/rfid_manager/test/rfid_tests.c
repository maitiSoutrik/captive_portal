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

#define countof(x) (sizeof(x) / sizeof(x[0]))

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
