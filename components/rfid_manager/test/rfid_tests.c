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

TEST_CASE("RFID Manager: INIT", "ESP_OK")
{
    esp_err_t ret = rfid_manager_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

TEST_CASE("RFID Manager: Adding Card", "ESP_OK")
{

    // Add a rfid card
    esp_err_t ret = rfid_manager_add_card(0x12345678, "Test Card");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

TEST_CASE("RFID Manager: Getting Card", "ESP_OK")
{
    // Check if the card was added correctly
    rfid_card_t card;
    esp_err_t ret = rfid_manager_get_card(0x12345678, &card);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    TEST_ASSERT_EQUAL_UINT32(0x12345678, card.card_id);
    TEST_ASSERT_EQUAL_STRING("Test Card", card.name);

    TEST_ASSERT_EQUAL_UINT8(1, card.active);
}

TEST_CASE("Mean of an empty array is zero", "[mean]")
{
    const int values[] = {0};
    TEST_ASSERT_EQUAL(0, testable_mean(values, 0));
}

TEST_CASE("Mean of a test vector", "[mean]")
{
    const int v[] = {1, 3, 5, 7, 9};
    TEST_ASSERT_EQUAL(5, testable_mean(v, countof(v)));
}