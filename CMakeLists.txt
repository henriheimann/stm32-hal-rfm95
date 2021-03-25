add_library(stm32-hal-rfm95 rfm95.c rfm95.h lib/ideetron/AES-128_V10.c lib/ideetron/AES-128_V10.h lib/ideetron/Encrypt_V31.c lib/ideetron/Encrypt_V31.h)
target_include_directories(stm32-hal-rfm95 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
