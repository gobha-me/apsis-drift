cmake_minimum_required(VERSION 3.28)

project(apsis-drift VERSION 0.3.0 LANGUAGES CXX)

include(CTest)
include(FetchContent)

option(APSIS_DRIFT_TESTS "Build the Apsis Drift tests" ON)
set(APSIS_DRIFT_TERMFORGE_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../termforge"
    CACHE PATH "Path to a TermForge source checkout")
set(APSIS_DRIFT_TERMFORGE_GIT_TAG
    "v0.32.0"
    CACHE STRING "Pinned TermForge revision used by FetchContent")

find_package(termforge 0.32.0 CONFIG QUIET)
if (NOT TARGET termforge::lib)
  set(termforge_TESTS OFF CACHE BOOL "" FORCE)
  set(termforge_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(termforge_BIN OFF CACHE BOOL "" FORCE)
  set(termforge_BENCH OFF CACHE BOOL "" FORCE)
  set(termforge_INSTALL OFF CACHE BOOL "" FORCE)

  set(APSIS_DRIFT_TERMFORGE_SOURCE_COMPATIBLE FALSE)
  if (EXISTS "${APSIS_DRIFT_TERMFORGE_SOURCE_DIR}/CMakeLists.txt")
    if (TERMFORGE_VERSION VERSION_GREATER_EQUAL 0.32.0)
      set(APSIS_DRIFT_TERMFORGE_SOURCE_COMPATIBLE TRUE)
    else ()
      find_package(Git QUIET)
      if (GIT_FOUND)
        execute_process(
          COMMAND "${GIT_EXECUTABLE}" -C
                  "${APSIS_DRIFT_TERMFORGE_SOURCE_DIR}"
                  merge-base --is-ancestor v0.32.0 HEAD
          RESULT_VARIABLE APSIS_DRIFT_TERMFORGE_ANCESTOR_RESULT
          OUTPUT_QUIET
          ERROR_QUIET
        )
        if (APSIS_DRIFT_TERMFORGE_ANCESTOR_RESULT EQUAL 0)
          set(APSIS_DRIFT_TERMFORGE_SOURCE_COMPATIBLE TRUE)
        endif ()
      endif ()
    endif ()
  endif ()

  if (APSIS_DRIFT_TERMFORGE_SOURCE_COMPATIBLE)
    add_subdirectory(
      "${APSIS_DRIFT_TERMFORGE_SOURCE_DIR}"
      "${CMAKE_CURRENT_BINARY_DIR}/_deps/termforge-build"
      EXCLUDE_FROM_ALL
    )
  else ()
    if (EXISTS "${APSIS_DRIFT_TERMFORGE_SOURCE_DIR}/CMakeLists.txt")
      message(STATUS
        "Ignoring TermForge source checkout older than v0.32.0: "
        "${APSIS_DRIFT_TERMFORGE_SOURCE_DIR}")
    endif ()
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
  src/benchmark.cpp
  src/cockpit.cpp
  src/flight_deck_acceptance.cpp
  src/flight_input.cpp
  src/landscape.cpp
  src/menu.cpp
  src/render_profile.cpp
  src/seed.cpp
  src/simulation.cpp
  src/title.cpp
)
add_library(apsis-drift::lib ALIAS apsis_drift_lib)

target_include_directories(apsis_drift_lib
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
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
  target_include_directories(apsis-drift-tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
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

  add_test(NAME benchmark-sweep-report
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_sweep.cmake")

  add_test(NAME benchmark-fixed-step-determinism
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_fixed_step_benchmark.cmake")

  add_test(NAME flight-deck-acceptance-matrix
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_flight_deck_acceptance.cmake")

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
  add_test(NAME reject-sweep-zero-frames
    COMMAND apsis-drift --sweep 0 --report sweep-invalid.json)
  add_test(NAME reject-sweep-missing-report
    COMMAND apsis-drift --sweep 1)
  add_test(NAME reject-sweep-invalid-fps
    COMMAND apsis-drift --sweep 1 --sweep-fps 30,0
            --report sweep-invalid.json)
  add_test(NAME reject-sweep-duplicate-viewport
    COMMAND apsis-drift --sweep 1 --sweep-viewports remote,320x240
            --report sweep-invalid.json)
  add_test(NAME reject-sweep-incompatible-profile
    COMMAND apsis-drift --sweep 1 --profile remote
            --report sweep-invalid.json)
  add_test(NAME reject-sweep-options-without-mode
    COMMAND apsis-drift --sweep-fps 30 --report sweep-invalid.json)
  add_test(NAME reject-keyboard-with-automatic-driver
    COMMAND apsis-drift --keyboard enhanced)
  add_test(NAME reject-acceptance-missing-report
    COMMAND apsis-drift --flight-deck-acceptance)
  add_test(NAME reject-acceptance-seed-override
    COMMAND apsis-drift --flight-deck-acceptance --seed 42
            --report acceptance-invalid.json)
  add_test(NAME reject-acceptance-mode-conflict
    COMMAND apsis-drift --flight-deck-acceptance --benchmark 1
            --report acceptance-invalid.json)
  add_test(NAME capability-floor-refusal
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_capability_refusal.cmake")
  set_tests_properties(
    reject-viewport-zero
    reject-viewport-negative
    reject-viewport-malformed
    reject-viewport-overflow
    reject-viewport-axis
    reject-viewport-pixel-budget
    reject-sweep-zero-frames
    reject-sweep-missing-report
    reject-sweep-invalid-fps
    reject-sweep-duplicate-viewport
    reject-sweep-incompatible-profile
    reject-sweep-options-without-mode
    reject-keyboard-with-automatic-driver
    reject-acceptance-missing-report
    reject-acceptance-seed-override
    reject-acceptance-mode-conflict
    PROPERTIES WILL_FAIL TRUE
  )
endif ()
