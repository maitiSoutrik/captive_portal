# Create a variable with all the files to embed
set(WEB_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/webpage")

# Register the component and embed the files
idf_component_register(SRCS "main.c" "dns_server.c"
                    INCLUDE_DIRS "include")

# Embed each file individually with a specific name pattern
target_add_binary_data(${COMPONENT_TARGET} "${WEB_SRC_DIR}/index.html" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "${WEB_SRC_DIR}/app.js" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "${WEB_SRC_DIR}/app.css" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "${WEB_SRC_DIR}/jquery-3.3.1.min.js" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "${WEB_SRC_DIR}/favicon.ico" BINARY)
