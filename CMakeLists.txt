cmake_minimum_required(VERSION 3.16)

# Components that live in top-level folders (ESP-IDF only auto-finds
# components/ and main/).
set(EXTRA_COMPONENT_DIRS font render page)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(e-reader)
