
cmake_minimum_required(VERSION 3.16)

project(cpp_secure_proxy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Use Debug mode by default if no build type is specified.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Enable useful compiler warnings.
add_compile_options(-Wall -Wextra)

# Link threading support.
find_package(Threads REQUIRED)

add_executable(cpp_secure_proxy
    src/main.cpp
)

target_link_libraries(cpp_secure_proxy PRIVATE Threads::Threads)

add_executable(proxy_client
    src/client.cpp
)

target_link_libraries(proxy_client PRIVATE Threads::Threads)
