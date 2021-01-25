cmake_minimum_required(VERSION 3.7)
project(rpi-i2c-classic-controller)
include(CheckIncludeFile)

add_executable(i2c-classic-controller i2c-classic-controller.c)
set(CMAKE_C_FLAGS "-O3")

install(FILES service/99-nintendo-i2c-controller.rules DESTINATION /etc/udev/rules.d)
install(FILES service/nintendo-i2c-controller.service DESTINATION /etc/systemd/system)
install(TARGETS i2c-classic-controller DESTINATION /usr/local/bin)

check_include_file(i2c/smbus.h HAVE_SMBUS_H)
if(HAVE_SMBUS_H)
    add_compile_definitions(HAVE_SMBUS_H)
endif()
find_library(I2CLIB i2c)
if(I2CLIB)
    target_link_libraries(i2c-classic-controller ${I2CLIB})
endif()
