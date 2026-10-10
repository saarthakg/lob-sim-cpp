cmake_minimum_required(VERSION 3.20)
project(lob_sim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── compiler flags ────────────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_compile_options(-O3)
endif()

# ── FetchContent ──────────────────────────────────────────────────────────────
include(FetchContent)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.3
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

# IXWebSocket — for live Binance diff-depth capture
set(USE_TLS ON  CACHE BOOL "" FORCE)
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  ixwebsocket
  GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
  GIT_TAG        v11.4.5
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(ixwebsocket)

# ── lob_core library ──────────────────────────────────────────────────────────
file(GLOB_RECURSE LOB_CORE_SRCS
  "cpp/src/core/*.cpp"
  "cpp/src/marketdata/*.cpp"
  "cpp/src/sim/*.cpp"
  "cpp/src/strategy/*.cpp"
)

add_library(lob_core STATIC ${LOB_CORE_SRCS})
target_include_directories(lob_core PUBLIC cpp/include)
target_link_libraries(lob_core PUBLIC nlohmann_json::nlohmann_json)

# ── lob_sim executable ────────────────────────────────────────────────────────
add_executable(lob_sim cpp/app/main.cpp)
target_link_libraries(lob_sim PRIVATE lob_core ixwebsocket)

# ── lob_tests executable ─────────────────────────────────────────────────────
file(GLOB TEST_SRCS "cpp/tests/*.cpp")
add_executable(lob_tests ${TEST_SRCS})
target_link_libraries(lob_tests PRIVATE lob_core Catch2::Catch2WithMain)
# Catch2's TEST_CASE uses __COUNTER__, which newer Clang flags under -Wpedantic.
target_compile_options(lob_tests PRIVATE -Wno-c2y-extensions)

include(CTest)
include(Catch)
catch_discover_tests(lob_tests)

# ── lob_bench executable ──────────────────────────────────────────────────────
add_executable(lob_bench cpp/bench/bench_replay.cpp)
target_link_libraries(lob_bench PRIVATE lob_core)

add_executable(lob_bench_l3 cpp/bench/bench_l3.cpp)
target_link_libraries(lob_bench_l3 PRIVATE lob_core)
