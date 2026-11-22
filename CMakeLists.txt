cmake_minimum_required(VERSION 3.16)
project(codelens CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_APP "Build the raylib app (OFF = engine and tests only)" ON)

include(FetchContent)

# nlohmann/json (single header) — used by the AI client
FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(json)

find_package(Threads REQUIRED)

# ---------- Engine (no raylib) ----------
file(GLOB CORE_SRC CONFIGURE_DEPENDS src/core/*.cpp)
add_library(core STATIC ${CORE_SRC})
target_include_directories(core PUBLIC src/core)
target_link_libraries(core PUBLIC nlohmann_json::nlohmann_json Threads::Threads)
target_compile_options(core PRIVATE -Wall -Wextra)
target_compile_definitions(core PRIVATE CODELENS_CORE_ROOT="${CMAKE_SOURCE_DIR}/src/core")

# ---------- Tests ----------
add_executable(tests tests/tests.cpp)
target_link_libraries(tests PRIVATE core)
target_compile_definitions(tests PRIVATE LESSONS_DIR="${CMAKE_SOURCE_DIR}/lessons"
                                         FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
# Regenerates tests/fixtures/*.json (memory snapshots for the visualizer)
add_executable(make_fixtures tests/make_fixtures.cpp)
target_link_libraries(make_fixtures PRIVATE core)
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
  list(REMOVE_ITEM UI_SRC "${CMAKE_SOURCE_DIR}/src/ui/main.cpp")
  add_library(codelens_ui STATIC ${UI_SRC})
  target_link_libraries(codelens_ui PUBLIC core raylib)
  target_include_directories(codelens_ui PUBLIC src/ui)
  target_compile_options(codelens_ui PRIVATE -Wall -Wextra)
  add_executable(codelens src/ui/main.cpp)
  target_link_libraries(codelens PRIVATE codelens_ui)
  add_executable(ui_tests tests/ui_tests.cpp)
  target_link_libraries(ui_tests PRIVATE codelens_ui)
  add_executable(ui_interaction_checks src/ui/checks/interactions.cpp)
  target_link_libraries(ui_interaction_checks PRIVATE codelens_ui)
  target_compile_options(ui_interaction_checks PRIVATE -Wall -Wextra)
  # The app finds lessons/ and assets/ next to the source tree by default
  target_compile_definitions(codelens PRIVATE CODELENS_ROOT="${CMAKE_SOURCE_DIR}")
endif()
