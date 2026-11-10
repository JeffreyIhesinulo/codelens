cmake_minimum_required(VERSION 3.16)
project(codelens CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_APP "Build the raylib app (OFF = engine and tests only)" ON)

include(FetchContent)

# nlohmann/json (single header) — used by the AI client
FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_MakeAvailable(json)

find_package(Threads REQUIRED)

# ---------- Engine (no raylib) ----------
file(GLOB CORE_SRC CONFIGURE_DEPENDS src/core/*.cpp)
add_library(core STATIC ${CORE_SRC})
target_include_directories(core PUBLIC src/core)
target_link_libraries(core PUBLIC nlohmann_json::nlohmann_json Threads::Threads)
target_compile_options(core PRIVATE -Wall -Wextra)

# ---------- Tests ----------
add_executable(tests tests/tests.cpp)
target_link_libraries(tests PRIVATE core)
target_compile_definitions(tests PRIVATE LESSONS_DIR="${CMAKE_SOURCE_DIR}/lessons")
enable_testing()
add_test(NAME core_tests COMMAND tests)

# ---------- App ----------
if(BUILD_APP)
  FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
    GIT_SHALLOW    TRUE)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(raylib)

  file(GLOB UI_SRC CONFIGURE_DEPENDS src/ui/*.cpp)
  add_executable(codelens ${UI_SRC})
  target_link_libraries(codelens PRIVATE core raylib)
  target_compile_options(codelens PRIVATE -Wall -Wextra)
  # The app finds lessons/ and assets/ next to the source tree by default
  target_compile_definitions(codelens PRIVATE CODELENS_ROOT="${CMAKE_SOURCE_DIR}")
endif()
