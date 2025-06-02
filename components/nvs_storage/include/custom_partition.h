#ifndef CUSTOM_PARTITION_H
#define CUSTOM_PARTITION_H

#include <stdbool.h>
#include <stddef.h> // For size_t

/**
 * @brief Sets a key-value pair in the custom NVS partition ("params").
 *
 * @param key The key (null-terminated string).
 * @param value The value to store (null-terminated string).
 * @return true if the operation was successful, false otherwise.
 */
bool nvs_custom_partition_set_params(const char *key, const char *value);

/**
 * @brief Gets a value for a given key from the custom NVS partition ("params").
 *
 * @param key The key (null-terminated string).
 * @param value Buffer to store the retrieved value.
 * @param value_len Maximum length of the value buffer.
 * @return true if the operation was successful and the key was found, false otherwise.
 */
bool nvs_custom_partition_get_params(const char *key, char *value, size_t value_len);

/**
 * @brief Tests the set and get operations on the custom NVS partition.
 *        Logs results to the console.
 */
void nvs_custom_partition_test(void);

#endif // CUSTOM_PARTITION_H
