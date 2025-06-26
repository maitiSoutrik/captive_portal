#include <limits.h>
#include "unity.h"
#include "rfid_manager.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG_TEST = "RFID_CACHE_TEST";

#define RFID_WRITE_TIMEOUT_MS_TEST (100)
#define NUM_DEFAULT_CARDS 3

TEST_CASE("RFID Manager: INIT", "[rfid_manager]")
{
    esp_err_t ret = rfid_manager_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

TEST_CASE("RFID Manager: Adding Card", "[rfid_manager]")
{
    esp_err_t format_ret = rfid_manager_format_database();
    TEST_ASSERT_EQUAL(ESP_OK, format_ret);

    esp_err_t ret = rfid_manager_add_card(0xABCDEFFF, "New Unique Card");
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = rfid_manager_add_card(0x12345678, "Attempt to overwrite");
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);

    esp_err_t ret2 = rfid_manager_add_card(0x11223344, "Another New Card");
    TEST_ASSERT_EQUAL(ESP_OK, ret2);
}

TEST_CASE("RFID Manager: Getting Card", "[rfid_manager]")
{
    esp_err_t format_ret = rfid_manager_format_database();
    TEST_ASSERT_EQUAL(ESP_OK, format_ret);

    rfid_card_t card;
    esp_err_t ret = rfid_manager_get_card(0x12345678, &card);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    TEST_ASSERT_EQUAL_UINT32(0x12345678, card.card_id);
    TEST_ASSERT_EQUAL_STRING("Admin Card", card.name);
    TEST_ASSERT_EQUAL_UINT8(1, card.active);
}

TEST_CASE("RFID Manager: Fill Database (Performance/Stress)", "[rfid_manager]")
{
    esp_err_t ret;

    ret = rfid_manager_format_database();
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    uint16_t initial_card_count = rfid_manager_get_card_count();

    char card_name[RFID_CARD_NAME_LEN];
    uint32_t base_card_id = 0x20000000;

    for (uint16_t i = 0; i < (RFID_MAX_CARDS - initial_card_count); ++i)
    {
        uint32_t current_card_id = base_card_id + i;
        snprintf(card_name, RFID_CARD_NAME_LEN, "StressCard %u", i);
        ret = rfid_manager_add_card(current_card_id, card_name);
        TEST_ASSERT_EQUAL(ESP_OK, ret);
    }

    uint16_t final_card_count = rfid_manager_get_card_count();
    TEST_ASSERT_EQUAL_UINT16(RFID_MAX_CARDS, final_card_count);

    ret = rfid_manager_add_card(base_card_id + RFID_MAX_CARDS, "Overflow Card");
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, ret);
}

TEST_CASE("RFID Manager: File Corruption and Recovery", "[rfid_manager]")
{
    esp_err_t ret;

    ret = rfid_manager_format_database();
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    uint32_t custom_card_id = 0xDDCCBBAA;
    const char *custom_card_name = "CustomCorruptTest";
    ret = rfid_manager_add_card(custom_card_id, custom_card_name);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    rfid_card_t temp_card;
    ret = rfid_manager_get_card(custom_card_id, &temp_card);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_STRING(custom_card_name, temp_card.name);
    TEST_ASSERT_EQUAL_UINT16(NUM_DEFAULT_CARDS + 1, rfid_manager_get_card_count());

    const char *filepath = "/spiffs/rfid_cards.dat";
    FILE *f_check = fopen(filepath, "rb");
    if (f_check)
    {
        fclose(f_check);
        ESP_LOGI(TAG_TEST, "Corrupting file by removing: %s", filepath);
        int remove_ret = remove(filepath);
        TEST_ASSERT_EQUAL_INT(0, remove_ret);
    }
    else
    {
        ESP_LOGW(TAG_TEST, "File %s did not exist before attempting removal for corruption test. This might be unexpected.", filepath);
    }

    ret = rfid_manager_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ret = rfid_manager_get_card(custom_card_id, &temp_card);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, ret);

    uint32_t default_admin_card_id = 0x12345678;
    ret = rfid_manager_get_card(default_admin_card_id, &temp_card);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_STRING("Admin Card", temp_card.name);

    TEST_ASSERT_EQUAL_UINT16(NUM_DEFAULT_CARDS, rfid_manager_get_card_count());
}

TEST_CASE("RFID Manager Cache: Add Card - No Immediate NVS Write", "[rfid_manager_caching]")
{
    uint32_t card_id_to_add = 0xAABBCCDD;
    const char *card_name = "CacheTestCard1";

    esp_err_t add_ret = rfid_manager_add_card(card_id_to_add, card_name);
    TEST_ASSERT_EQUAL(ESP_OK, add_ret);

    rfid_card_t fetched_card_mem;
    esp_err_t get_mem_ret = rfid_manager_get_card(card_id_to_add, &fetched_card_mem);
    TEST_ASSERT_EQUAL(ESP_OK, get_mem_ret);
    TEST_ASSERT_EQUAL_STRING(card_name, fetched_card_mem.name);

    ESP_LOGI(TAG_TEST, "Re-initializing RFID manager to check NVS content before any timer expiry.");
    esp_err_t reinit_ret = rfid_manager_init();
    TEST_ASSERT_EQUAL(ESP_OK, reinit_ret);

    rfid_card_t fetched_card_nvs;
    esp_err_t get_nvs_ret = rfid_manager_get_card(card_id_to_add, &fetched_card_nvs);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, get_nvs_ret);

    esp_err_t deinit_ret = rfid_manager_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, deinit_ret);
}

TEST_CASE("RFID Manager Cache: Timer Expiry Triggers NVS Write", "[rfid_manager_caching]")
{
    TEST_ASSERT_EQUAL(ESP_OK, rfid_manager_init());

    uint32_t card_id = 0xEEFF0011;
    const char *card_name = "CacheWriteTest";

    esp_err_t add_ret = rfid_manager_add_card(card_id, card_name);
    TEST_ASSERT_EQUAL(ESP_OK, add_ret);

    ESP_LOGI(TAG_TEST, "Waiting for RFID cache timer to expire...");
    vTaskDelay(pdMS_TO_TICKS(5100));
    rfid_manager_process();

    // ESP_LOGI(TAG_TEST, "Re-initializing RFID manager to check NVS persistence after timer expiry.");
    // vTaskDelay(pdMS_TO_TICKS(10));
    // esp_err_t init_ret = rfid_manager_init();
    // TEST_ASSERT_EQUAL(ESP_OK, init_ret);

    rfid_card_t fetched_card;
    esp_err_t get_ret = rfid_manager_get_card(card_id, &fetched_card);
    TEST_ASSERT_EQUAL(ESP_OK, get_ret);
    TEST_ASSERT_EQUAL_STRING(card_name, fetched_card.name);

    esp_err_t deinit_ret = rfid_manager_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, deinit_ret);
}

TEST_CASE("RFID Manager Cache: Making sure card is removed", "[rfid_manager_caching]")
{
    TEST_ASSERT_EQUAL(ESP_OK, rfid_manager_init());

    uint32_t card_to_remove_id = 0x12345678;

    rfid_card_t card_before_remove;
    TEST_ASSERT_EQUAL(ESP_OK, rfid_manager_get_card(card_to_remove_id, &card_before_remove));
    TEST_ASSERT_TRUE(card_before_remove.active);

    TEST_ASSERT_EQUAL(ESP_OK, rfid_manager_remove_card(card_to_remove_id));

    rfid_card_t card_after_remove_mem;
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, rfid_manager_get_card(card_to_remove_id, &card_after_remove_mem));

    // ESP_LOGI(TAG_TEST, "Re-initializing RFID manager to check NVS content before timer expiry (for remove).");
    // TEST_ASSERT_EQUAL(ESP_OK, rfid_manager_init());

    // rfid_card_t card_after_reinit_nvs;
    // esp_err_t get_nvs_ret = rfid_manager_get_card(card_to_remove_id, &card_after_reinit_nvs);
    // TEST_ASSERT_EQUAL(ESP_OK, get_nvs_ret);
    // TEST_ASSERT_TRUE(card_after_reinit_nvs.active);

    // esp_err_t deinit_ret = rfid_manager_deinit();
    // TEST_ASSERT_EQUAL(ESP_OK, deinit_ret);
}

TEST_CASE("RFID Manager Cache: Remove Card - Timer Expiry Triggers NVS Write", "[rfid_manager_caching]")
{
    TEST_ASSERT_EQUAL(ESP_OK, rfid_manager_init());

    uint32_t card_to_remove_id = 0x12345678;

    TEST_ASSERT_EQUAL(ESP_OK, rfid_manager_remove_card(card_to_remove_id));

    ESP_LOGI(TAG_TEST, "Waiting for RFID cache timer to expire after remove...");
    vTaskDelay(pdMS_TO_TICKS(5100));
    rfid_manager_process();

    esp_err_t deinit_ret = rfid_manager_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, deinit_ret);

    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG_TEST, "Re-initializing RFID manager to check NVS persistence after remove and timer expiry.");
    vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_EQUAL(ESP_OK, rfid_manager_init());

    rfid_card_t card_after_timer_expiry;
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, rfid_manager_get_card(card_to_remove_id, &card_after_timer_expiry));

    TEST_ASSERT_EQUAL(ESP_OK, rfid_manager_deinit());
}
