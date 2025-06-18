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
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h" // For direct NVS interaction if needed for setup/teardown

#define countof(x) (sizeof(x) / sizeof(x[0]))

static const char* TAG_TEST = "RFID_CACHE_TEST";

// Define a shorter timeout for testing purposes
// This could be set via Kconfig for test builds, or conditionally compiled.
// IMPORTANT: The rfid_manager component will need to be modified to use this
// value during tests, e.g., via a function rfid_manager_set_write_timeout_for_test()
// or by compiling with a TEST_BUILD flag.
#define RFID_WRITE_TIMEOUT_MS_TEST (100) // 100ms for tests

// Helper to ensure a known starting state for RFID NVS (loads defaults)
// and initializes the rfid_manager.
static void ensure_clean_rfid_nvs_and_init_manager(void) {
    TEST_ASSERT_EQUAL(ESP_OK, rfid_manager_format_database());
    // Explicitly call rfid_manager_init() after formatting to ensure all initializations,
    // including timer creation, are done. format_database handles mutex and data,
    // but init handles timer creation.
    TEST_ASSERT_EQUAL(ESP_OK, rfid_manager_init());
    // If a test-specific timeout needs to be set:
    rfid_manager_set_cache_write_timeout(RFID_WRITE_TIMEOUT_MS_TEST); 
}

// setUp function, called before each test in the group using it (not standard in current file, but good for new tests)
void setUp(void) {
    // Ensure NVS is initialized for the test partition
    esp_err_t ret = nvs_flash_init_partition("nvs"); // Default NVS partition
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        TEST_ASSERT_EQUAL(ESP_OK, nvs_flash_erase_partition("nvs"));
        ret = nvs_flash_init_partition("nvs");
    }
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    ensure_clean_rfid_nvs_and_init_manager();
}

// tearDown function, called after each test
void tearDown(void) {
    // Optional: could deinit rfid_manager if a deinit function is implemented
    // rfid_manager_deinit(); // This function needs to be implemented in rfid_manager.c
    // nvs_flash_deinit_partition("nvs"); // Usually not needed if re-init in setUp
}
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

/* --- Test Cases for RFID Manager Caching --- */

// Note: These tests assume that rfid_manager has been modified to:
// 1. Implement caching logic.
// 2. Potentially use RFID_WRITE_TIMEOUT_MS_TEST during tests (e.g., via a setter or conditional compilation).
// 3. Potentially expose an is_dirty flag or similar for more direct testing (e.g., rfid_manager_is_dirty_internal()).

TEST_CASE("RFID Manager Cache: Add Card - No Immediate NVS Write", "[rfid_manager_caching]")
{
    setUp(); // Ensures clean NVS (defaults loaded) and manager initialized.
             // At this point, NVS and in-memory rfid_database contain only default cards.

    uint32_t card_id_to_add = 0xAABBCCDD;
    const char* card_name = "CacheTestCard1";

    // Add the new card.
    // With current (non-caching) code, this writes to NVS immediately.
    // With caching code, this should update memory only and start a timer.
    esp_err_t add_ret = rfid_manager_add_card(card_id_to_add, card_name);
    TEST_ASSERT_EQUAL(ESP_OK, add_ret);

    // Verify it's in the current in-memory representation
    // (assuming rfid_manager_get_card reads the live in-memory state)
    rfid_card_t fetched_card_mem;
    esp_err_t get_mem_ret = rfid_manager_get_card(card_id_to_add, &fetched_card_mem);
    TEST_ASSERT_EQUAL(ESP_OK, get_mem_ret);
    TEST_ASSERT_EQUAL_STRING(card_name, fetched_card_mem.name);

    // 2. Verify is_dirty flag is set (requires a test helper in rfid_manager.c, to be added later)
    // TEST_ASSERT_TRUE(rfid_manager_is_dirty_internal());

    // 3. Verify NVS was *not* written immediately.
    // Re-initialize the manager. This forces a load from the NVS file.
    // If the write of card_id_to_add was truly delayed (i.e., caching is working and timer hasn't fired),
    // NVS should still contain the original default set, NOT the newly added card.
    ESP_LOGI(TAG_TEST, "Re-initializing RFID manager to check NVS content before any timer expiry.");
    esp_err_t reinit_ret = rfid_manager_init(); // This reloads from NVS.
    TEST_ASSERT_EQUAL(ESP_OK, reinit_ret);

    // Try to get the card again. Since rfid_manager_init() reloaded from NVS,
    // and NVS shouldn't have the new card yet if caching is working, this should fail.
    // WITH CURRENT CODE (no caching): This assertion will FAIL because add_card writes immediately to NVS,
    // so rfid_manager_init() will load it, and get_card will find it.
    rfid_card_t fetched_card_nvs;
    esp_err_t get_nvs_ret = rfid_manager_get_card(card_id_to_add, &fetched_card_nvs);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, get_nvs_ret); // This assertion should now fail with current code.

    tearDown();
}

TEST_CASE("RFID Manager Cache: Timer Expiry Triggers NVS Write", "[rfid_manager_caching]")
{
    setUp(); // Ensure clean state and test NVS init

    // It's assumed rfid_manager is now using RFID_WRITE_TIMEOUT_MS_TEST
    // e.g., via rfid_manager_set_write_timeout_for_test(RFID_WRITE_TIMEOUT_MS_TEST);
    // called in setUp or ensure_clean_rfid_nvs_and_init_manager, or via conditional compilation.

    uint32_t card_id = 0xEEFF0011;
    const char* card_name = "CacheWriteTest";

    esp_err_t add_ret = rfid_manager_add_card(card_id, card_name);
    TEST_ASSERT_EQUAL(ESP_OK, add_ret);

    ESP_LOGI(TAG_TEST, "Waiting for RFID cache timer (%d ms) to expire...", RFID_WRITE_TIMEOUT_MS_TEST);
    vTaskDelay(pdMS_TO_TICKS(RFID_WRITE_TIMEOUT_MS_TEST + 50)); // Wait for timer + buffer

    // Now, re-initialize the manager to force a load from NVS.
    // If the caching logic worked and wrote to NVS upon timer expiry, the card should be found.
    ESP_LOGI(TAG_TEST, "Re-initializing RFID manager to check NVS persistence after timer expiry.");
    // rfid_manager_init(); // Calling init directly. ensure_clean_rfid_nvs_and_init_manager() would format.
    // We need to ensure that rfid_manager_init() itself will load from NVS.
    // The current rfid_manager_init likely does this.
    // If rfid_manager_init() is not idempotent or doesn't reload without format, this test needs adjustment
    // or a specific "load from NVS" function.
    // For now, let's assume a fresh rfid_manager_init() after the component is modified will load the latest from NVS.
    // A safer way for testing might be to de-init and re-init if those functions are robust.
    // Or, if rfid_manager_init is already called by setUp, we might need to call format and then init
    // to truly simulate a fresh start that reads from NVS.
    // However, formatting would erase the card we just (hopefully) wrote.
    // So, the most straightforward is to call rfid_manager_init() again, assuming it reloads.
    // If the rfid_manager is a singleton and init is not meant to be called multiple times without deinit,
    // this test strategy needs refinement based on rfid_manager's final design.

    // Let's try re-initializing after a brief delay to ensure NVS operations complete.
    vTaskDelay(pdMS_TO_TICKS(10)); // Small delay for NVS write to settle if async (though usually sync)
    esp_err_t init_ret = rfid_manager_init(); // Attempt to re-initialize, hoping it loads from NVS
    TEST_ASSERT_EQUAL(ESP_OK, init_ret);


    rfid_card_t fetched_card;
    esp_err_t get_ret = rfid_manager_get_card(card_id, &fetched_card);
    TEST_ASSERT_EQUAL(ESP_OK, get_ret); // Card should now be found from NVS
    TEST_ASSERT_EQUAL_STRING(card_name, fetched_card.name);

    // Verify is_dirty flag is false now (requires a test helper)
    // TEST_ASSERT_FALSE(rfid_manager_is_dirty_internal()); // Example

    tearDown();
}

TEST_CASE("RFID Manager Cache: Remove Card - No Immediate NVS Write", "[rfid_manager_caching]")
{
    setUp(); // Ensures defaults are in NVS and memory. Admin card (0x12345678) is active.

    uint32_t card_to_remove_id = 0x12345678; // Default Admin Card

    // Verify card is initially active and present
    rfid_card_t card_before_remove;
    TEST_ASSERT_EQUAL(ESP_OK, rfid_manager_get_card(card_to_remove_id, &card_before_remove));
    TEST_ASSERT_TRUE(card_before_remove.active);

    // Remove the card. This should mark it inactive in memory and start the timer.
    // NVS should not be updated immediately.
    TEST_ASSERT_EQUAL(ESP_OK, rfid_manager_remove_card(card_to_remove_id));

    // Verify it's marked inactive in the current in-memory representation
    // rfid_manager_get_card returns ESP_ERR_NOT_FOUND for inactive cards.
    rfid_card_t card_after_remove_mem;
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, rfid_manager_get_card(card_to_remove_id, &card_after_remove_mem));

    // Re-initialize the manager. This forces a load from NVS.
    // If the write of the "inactive" state was truly delayed, NVS should still have the card as ACTIVE.
    ESP_LOGI(TAG_TEST, "Re-initializing RFID manager to check NVS content before timer expiry (for remove).");
    TEST_ASSERT_EQUAL(ESP_OK, rfid_manager_init()); // Reloads from NVS

    // Try to get the card again. Since NVS shouldn't have the "inactive" update yet,
    // it should still be found as active from NVS.
    rfid_card_t card_after_reinit_nvs;
    esp_err_t get_nvs_ret = rfid_manager_get_card(card_to_remove_id, &card_after_reinit_nvs);
    TEST_ASSERT_EQUAL(ESP_OK, get_nvs_ret); // Expect to find it because NVS not updated yet
    TEST_ASSERT_TRUE(card_after_reinit_nvs.active); // Expect it to be active in NVS

    tearDown();
}

TEST_CASE("RFID Manager Cache: Remove Card - Timer Expiry Triggers NVS Write", "[rfid_manager_caching]")
{
    setUp(); // Ensures defaults are in NVS and memory. Admin card (0x12345678) is active.

    uint32_t card_to_remove_id = 0x12345678; // Default Admin Card

    // Remove the card. This marks it inactive in memory and starts the timer.
    TEST_ASSERT_EQUAL(ESP_OK, rfid_manager_remove_card(card_to_remove_id));

    // Wait for the timer to expire. The test timeout is shorter.
    // The actual component timer uses RFID_WRITE_TIMEOUT_US.
    // For this test to be effective, the component should ideally use RFID_WRITE_TIMEOUT_MS_TEST
    // when in a test environment. If not, this delay might be too short for the real timer.
    // Assuming for now the component is somehow aware of the test timeout or we are patient.
    // Let's use the test-specific timeout for the delay.
    ESP_LOGI(TAG_TEST, "Waiting for RFID cache timer (%d ms) to expire after remove...", RFID_WRITE_TIMEOUT_MS_TEST);
    vTaskDelay(pdMS_TO_TICKS(RFID_WRITE_TIMEOUT_MS_TEST + 50)); // Wait for timer + buffer

    // Re-initialize the manager to force a load from NVS.
    // The timer should have fired and written the inactive state to NVS.
    ESP_LOGI(TAG_TEST, "Re-initializing RFID manager to check NVS persistence after remove and timer expiry.");
    vTaskDelay(pdMS_TO_TICKS(10)); // Small delay for NVS write to settle
    TEST_ASSERT_EQUAL(ESP_OK, rfid_manager_init()); // Reloads from NVS

    // Try to get the card. It should now be reported as not found (or inactive) from NVS.
    rfid_card_t card_after_timer_expiry;
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, rfid_manager_get_card(card_to_remove_id, &card_after_timer_expiry));

    tearDown();
}
