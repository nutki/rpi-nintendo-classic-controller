cmake_minimum_required(VERSION 3.7)
project(rpi-i2c-classic-controller)

add_executable(i2c-classic-controller i2c-classic-controller.c)
set(CMAKE_C_FLAGS "-O3")

install(FILES service/99-nintendo-i2c-controller.rules DESTINATION /etc/udev/rules.d)
install(FILES service/nintendo-i2c-controller.service DESTINATION /etc/systemd/system)
install(TARGETS i2c-classic-controller DESTINATION /usr/local/bin)
