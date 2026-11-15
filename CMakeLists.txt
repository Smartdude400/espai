# Top-level ESP-IDF project file for the "espai" Wikipedia voice gadget.
# Target board: Waveshare ESP32-S3-LCD-1.3 (ESP32-S3R8, 8MB octal PSRAM, 16MB flash)
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(espai_wiki)
