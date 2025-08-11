find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# Channel Sounding 
add_library(cs cs/cs.c)
target_include_directories(cs PUBLIC cs ${ZEPHYR_BASE}/include)
target_link_libraries(cs PUBLIC zephyr_interface calc calc_black_box)

# Bluetooth 
add_library(ble ble/ble.c)
target_include_directories(ble PUBLIC ble ${ZEPHYR_BASE}/include)
target_link_libraries(ble PUBLIC zephyr_interface cs sync)

# Distance calculation 
add_library(calc calc/calc.c)
target_include_directories(calc PUBLIC calc ${ZEPHYR_BASE}/include)
target_link_libraries(calc PUBLIC zephyr_interface)

# Synchronization 
add_library(sync sync/sync.c)
target_include_directories(sync PUBLIC sync ${ZEPHYR_BASE}/include)
target_link_libraries(sync PUBLIC zephyr_interface)

# Black box 
add_library(black_box INTERFACE)
target_link_libraries(black_box INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/black_box/libcoreaiot_distance.a)
target_include_directories(black_box INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/black_box)

# Distance calculation Black box version 
add_library(calc_black_box calc_black_box/calc_black_box.c)
target_include_directories(calc_black_box PUBLIC calc_black_box ${ZEPHYR_BASE}/include)
target_link_libraries(calc_black_box PUBLIC zephyr_interface black_box) 