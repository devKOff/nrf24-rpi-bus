cmake_minimum_required(VERSION 3.16)
project(pi-nrf24-link VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Werror -O2 -g")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")

find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(GPIOD REQUIRED gpiod)
pkg_check_modules(RF24 REQUIRED rf24)

add_executable(pi-nrf24-link
    main.cpp
    crypto_utils.cpp
    radio_handler.cpp
)

target_include_directories(pi-nrf24-link PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OPENSSL_INCLUDE_DIR}
    ${GPIOD_INCLUDE_DIRS}
    ${RF24_INCLUDE_DIRS}
)

target_link_directories(pi-nrf24-link PRIVATE
    ${GPIOD_LIBRARY_DIRS}
    ${RF24_LIBRARY_DIRS}
)

target_link_libraries(pi-nrf24-link PRIVATE
    ${OPENSSL_LIBRARIES}
    ${GPIOD_LIBRARIES}
    ${RF24_LIBRARIES}
    pthread
    rt
)

target_compile_options(pi-nrf24-link PRIVATE
    -Wno-unused-parameter
    -Wno-sign-compare
)

install(TARGETS pi-nrf24-link
    RUNTIME DESTINATION bin
)

install(FILES nrf24_comm.service
    DESTINATION lib/systemd/system
)

option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

option(BUILD_DOCS "Build documentation" OFF)
if(BUILD_DOCS)
    find_package(Doxygen)
    if(DOXYGEN_FOUND)
        add_subdirectory(docs)
    endif()
endif()