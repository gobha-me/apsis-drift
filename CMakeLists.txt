cmake_minimum_required(VERSION 3.28)

project(apsis-drift VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(FetchContent)

option(APSIS_DRIFT_TESTS "Build the Apsis Drift tests" ON)
set(APSIS_DRIFT_TERMFORGE_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../termforge"
    CACHE PATH "Path to a TermForge source checkout")
set(APSIS_DRIFT_TERMFORGE_GIT_TAG
    "0604d2087e326afcc99578a925a9b1012f5ba813"
    CACHE STRING "Pinned TermForge revision used by FetchContent")

find_package(termforge CONFIG QUIET)
if (NOT TARGET termforge::lib)
  set(termforge_TESTS OFF CACHE BOOL "" FORCE)
  set(termforge_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(termforge_BIN OFF CACHE BOOL "" FORCE)
  set(termforge_BENCH OFF CACHE BOOL "" FORCE)
  set(termforge_INSTALL OFF CACHE BOOL "" FORCE)

  if (EXISTS "${APSIS_DRIFT_TERMFORGE_SOURCE_DIR}/CMakeLists.txt")
    add_subdirectory(
      "${APSIS_DRIFT_TERMFORGE_SOURCE_DIR}"
      "${CMAKE_CURRENT_BINARY_DIR}/_deps/termforge-build"
      EXCLUDE_FROM_ALL
    )
  else ()
    message(STATUS
      "TermForge not installed or found at the sibling path; fetching "
      "${APSIS_DRIFT_TERMFORGE_GIT_TAG}")
    FetchContent_Declare(termforge
      GIT_REPOSITORY https://github.com/gobha-me/termforge.git
      GIT_TAG "${APSIS_DRIFT_TERMFORGE_GIT_TAG}"
      GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(termforge)
  endif ()
endif ()

add_library(apsis_drift_lib STATIC
  src/landscape.cpp
  src/render_profile.cpp
)
add_library(apsis-drift::lib ALIAS apsis_drift_lib)

target_include_directories(apsis_drift_lib
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_features(apsis_drift_lib PUBLIC cxx_std_23)
target_link_libraries(apsis_drift_lib PUBLIC termforge::lib)

add_executable(apsis-drift
  src/main.cpp
)
target_link_libraries(apsis-drift PRIVATE apsis-drift::lib)

if (APSIS_DRIFT_TESTS AND BUILD_TESTING)
  add_executable(apsis-drift-tests test/test.cpp)
  target_link_libraries(apsis-drift-tests PRIVATE apsis-drift::lib)
  add_test(NAME apsis-drift-tests COMMAND apsis-drift-tests)

  add_test(NAME benchmark-profile-remote
    COMMAND apsis-drift --benchmark 1 --profile remote)
  add_test(NAME benchmark-profile-balanced
    COMMAND apsis-drift --benchmark 1 --profile balanced)
  add_test(NAME benchmark-viewport-640x360
    COMMAND apsis-drift --benchmark 1 --viewport 640x360)
  add_test(NAME benchmark-profile-local
    COMMAND apsis-drift --benchmark 1 --profile local)
  add_test(NAME benchmark-viewport-800x600
    COMMAND apsis-drift --benchmark 1 --viewport 800x600)
  add_test(NAME benchmark-profile-cinematic
    COMMAND apsis-drift --benchmark 1 --profile cinematic)

  add_test(NAME reject-viewport-zero
    COMMAND apsis-drift --benchmark 1 --viewport 0x480)
  add_test(NAME reject-viewport-negative
    COMMAND apsis-drift --benchmark 1 --viewport -1x480)
  add_test(NAME reject-viewport-malformed
    COMMAND apsis-drift --benchmark 1 --viewport 640by480)
  add_test(NAME reject-viewport-overflow
    COMMAND apsis-drift --benchmark 1 --viewport
            999999999999999999999999x480)
  add_test(NAME reject-viewport-axis
    COMMAND apsis-drift --benchmark 1 --viewport 4097x1)
  add_test(NAME reject-viewport-pixel-budget
    COMMAND apsis-drift --benchmark 1 --viewport 4096x1025)
  set_tests_properties(
    reject-viewport-zero
    reject-viewport-negative
    reject-viewport-malformed
    reject-viewport-overflow
    reject-viewport-axis
    reject-viewport-pixel-budget
    PROPERTIES WILL_FAIL TRUE
  )
endif ()
