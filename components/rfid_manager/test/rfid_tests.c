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

    // Add a rfid card
    esp_err_t ret = rfid_manager_add_card(0x12345678, "Test Card");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

TEST_CASE("RFID Manager: Getting Card", "[rfid_manager]")
{
    // Check if the card was added correctly
    rfid_card_t card;
    esp_err_t ret = rfid_manager_get_card(0x12345678, &card);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    TEST_ASSERT_EQUAL_UINT32(0x12345678, card.card_id);
    TEST_ASSERT_EQUAL_STRING("Test Card", card.name);

    TEST_ASSERT_EQUAL_UINT8(1, card.active);
}

// Add a test to add 200 cards and benchmark time
// TEST_CASE("RFID Manager: Benchmarking Time", "[rfid_manager]")
// {
//     // Add 200 cards
//     for (int i = 0; i < RFID_MAX_CARDS; i++)
//     {
//         rfid_card_t card;
//         card.card_id = i + 1;
//         strcpy(card.name, "Test Card");
//         card.active = 1;
//         esp_err_t ret = rfid_manager_add_card(card.card_id, card.name);
//         TEST_ASSERT_EQUAL(ESP_OK, ret);
//     }
// }
