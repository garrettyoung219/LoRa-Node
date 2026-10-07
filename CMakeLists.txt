cmake_minimum_required(VERSION 3.13)



# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)


project(Project3)
pico_sdk_init()

# Pull in FreeRTOS
set(FREERTOS_KERNEL_PATH "C:\\Users\\garre\\Documents\\Pico-v1.5.1\\FreeRTOS\\FreeRTOS\\Source")
include(FreeRTOS_Kernel_import.cmake)

add_executable(project3_starter main.c
                        sx126x_hal.c
                        sx126x.c)
                                  
target_include_directories(project3_starter
                           PRIVATE
                           ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(project3_starter
                      pico_stdlib
                      hardware_spi
                      FreeRTOS-Kernel
                      pico_rand)

pico_add_extra_outputs(project3_starter)
pico_enable_stdio_usb(project3_starter 1)
pico_enable_stdio_uart(project3_starter 0)