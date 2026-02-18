cmake_minimum_required(VERSION 3.20)
project(ClawForge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)

FetchContent_Declare(
  cpp_httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.15.3
)

FetchContent_MakeAvailable(nlohmann_json cpp_httplib)

file(GLOB_RECURSE CLAWFORGE_SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_executable(clawforge ${CLAWFORGE_SOURCES})

target_include_directories(clawforge
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${cpp_httplib_SOURCE_DIR}
)

target_link_libraries(clawforge
  PRIVATE
    nlohmann_json::nlohmann_json
)

find_package(Threads REQUIRED)
target_link_libraries(clawforge PRIVATE Threads::Threads)

if(APPLE)
  target_compile_definitions(clawforge PRIVATE _DARWIN_C_SOURCE)
endif()

install(TARGETS clawforge DESTINATION bin)
