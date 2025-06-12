/* test_mean.c: Implementation of a testable component.

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <limits.h>
#include "unity.h"
#include "rfid_manager.h"

#define countof(x) (sizeof(x) / sizeof(x[0]))

TEST_CASE("INIT", "[RFID]")
{
    esp_err_t ret = rfid_manager_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

TEST_CASE("Adding Card", "[RFID]")
{

    // Add a rfid card
    esp_err_t ret = rfid_manager_add_card(0x12345678, "Test Card");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

TEST_CASE("Adding 200 Card", "[RFID]")
{
    // Add 200 rfid cards
    for (uint32_t i = 0; i < 200; ++i)
    {
        char name[RFID_CARD_NAME_LEN];
        snprintf(name, sizeof(name), "Card %lu", i);
        esp_err_t ret = rfid_manager_add_card(0x10000000 + i, name);
        TEST_ASSERT_EQUAL(ESP_OK, ret);
    }
}

TEST_CASE("Getting Card", "[RFID]")
{
    // Check if the card was added correctly
    rfid_card_t card;
    esp_err_t ret = rfid_manager_get_card(0x12345678, &card);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    TEST_ASSERT_EQUAL_UINT32(0x12345678, card.card_id);
    TEST_ASSERT_EQUAL_STRING("Test Card", card.name);

    TEST_ASSERT_EQUAL_UINT8(1, card.active);
}

TEST_CASE("Mean of an empty array is zero", "[RFID]")
{
    const int values[] = {0};
    TEST_ASSERT_EQUAL(0, testable_mean(values, 0));
}

TEST_CASE("Mean of a test vector", "[RFID]")
{
    const int v[] = {1, 3, 5, 7, 9};
    TEST_ASSERT_EQUAL(5, testable_mean(v, countof(v)));
}