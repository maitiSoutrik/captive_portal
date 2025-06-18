# CMakeLists.txt for rfid_manager component

# Define the source files for this component
set(COMPONENT_SRCS "rfid_manager.c")

# Define the include directories for this component
set(COMPONENT_ADD_INCLUDEDIRS "include")

# Specify component dependencies.
# This component will depend on SPIFFS for file storage.
# It might also depend on a logging component if ESP_LOG is used.
# Assuming 'spi_ffs_storage' is the component providing SPIFFS access
# and 'log' for ESP_LOGX functions.
# If spi_ffs_storage is not a separate component but part of esp-idf's spiffs,
# then 'spiffs' might be the dependency.
# For now, let's list common dependencies. Adjust as per actual project structure.

# IDF_COMPONENT_REQUIRES are for components that are part of ESP-IDF
# COMPONENT_REQUIRES are for other custom components in the project
# For SPIFFS, it's usually available through the 'spiffs' component in ESP-IDF.
# For logging, 'log' component.
# For FreeRTOS features (like mutexes), 'freertos'.

idf_component_register(SRCS "${COMPONENT_SRCS}"
                    INCLUDE_DIRS "${COMPONENT_ADD_INCLUDEDIRS}"
                    REQUIRES spi_ffs_storage log freertos)
# Note: 'spi_ffs_storage' is listed as a dependency in the issue.
# If it's a custom component, ensure its name is correct.
# If it's part of ESP-IDF or another library, adjust accordingly.
# 'log' for ESP_LOGx macros.
# 'freertos' for mutexes if used for concurrent access protection.