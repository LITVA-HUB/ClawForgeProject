cmake_minimum_required(VERSION 3.20)
project(NexaClaw VERSION 0.2.0 LANGUAGES CXX)

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

file(GLOB_RECURSE NEXACLAW_SOURCES CONFIGURE_DEPENDS src/*.cpp)

function(configure_nexa_target target_name)
  target_include_directories(${target_name}
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${cpp_httplib_SOURCE_DIR}
  )

  target_link_libraries(${target_name}
    PRIVATE
      nlohmann_json::nlohmann_json
      Threads::Threads
  )

  if(APPLE)
    target_compile_definitions(${target_name} PRIVATE _DARWIN_C_SOURCE)
  endif()
endfunction()

find_package(Threads REQUIRED)

# Primary binary (new branding)
add_executable(nexaclaw ${NEXACLAW_SOURCES})
configure_nexa_target(nexaclaw)

# Backward-compatibility alias binary
add_executable(clawforge ${NEXACLAW_SOURCES})
configure_nexa_target(clawforge)

install(TARGETS nexaclaw clawforge DESTINATION bin)
