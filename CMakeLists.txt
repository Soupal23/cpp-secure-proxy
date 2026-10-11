
cmake_minimum_required(VERSION 3.16)

project(cpp_secure_proxy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(cpp_secure_proxy
    src/main.cpp
)

add_executable(proxy_client
    src/client.cpp
)
