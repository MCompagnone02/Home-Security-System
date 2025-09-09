cmake_minimum_required(VERSION 3.16)
project(SecurityHostWindows)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# vcpkg packages
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Threads REQUIRED)

# Eseguibile
add_executable(security_host_windows security_host_windows.cpp)

# Link libraries
target_link_libraries(security_host_windows
        unofficial::sqlite3::sqlite3
        nlohmann_json::nlohmann_json
        Threads::Threads
        ws2_32
        wsock32
)

# Windows definitions
if(WIN32)
    target_compile_definitions(security_host_windows PRIVATE
            _WIN32_WINNT=0x0601
            WIN32_LEAN_AND_MEAN
    )
endif()
