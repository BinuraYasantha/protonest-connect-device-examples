# The following lines of boilerplate have to be in your project's CMakeLists.txt
# in this exact order for CMake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(protonest_x509_subscribe)

target_add_binary_data(protonest_x509_subscribe.elf "main/certs/root-ca.crt" TEXT)
target_add_binary_data(protonest_x509_subscribe.elf "main/certs/device-cert.pem" TEXT)
target_add_binary_data(protonest_x509_subscribe.elf "main/certs/device-key.pem" TEXT)
