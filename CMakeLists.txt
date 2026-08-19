cmake_minimum_required(VERSION 3.28)

project(apsis-drift VERSION 0.4.17 LANGUAGES CXX)

include(CTest)
include(FetchContent)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/include/apsis_drift/version.hpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/apsis_drift/version.hpp"
  @ONLY
)

option(APSIS_DRIFT_TESTS "Build the Apsis Drift tests" ON)
set(APSIS_DRIFT_TERMFORGE_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../termforge"
    CACHE PATH "Path to a TermForge source checkout")
set(APSIS_DRIFT_TERMFORGE_GIT_TAG
    "v0.42.0"
    CACHE STRING "Pinned TermForge revision used by FetchContent")

find_package(termforge 0.42.0 CONFIG QUIET)
if (NOT TARGET termforge::lib)
  set(termforge_TESTS OFF CACHE BOOL "" FORCE)
  set(termforge_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(termforge_BIN OFF CACHE BOOL "" FORCE)
  set(termforge_BENCH OFF CACHE BOOL "" FORCE)
  set(termforge_INSTALL OFF CACHE BOOL "" FORCE)

  set(APSIS_DRIFT_TERMFORGE_SOURCE_COMPATIBLE FALSE)
  if (EXISTS "${APSIS_DRIFT_TERMFORGE_SOURCE_DIR}/CMakeLists.txt")
    if (TERMFORGE_VERSION VERSION_GREATER_EQUAL 0.42.0)
      set(APSIS_DRIFT_TERMFORGE_SOURCE_COMPATIBLE TRUE)
    else ()
      find_package(Git QUIET)
      if (GIT_FOUND)
        execute_process(
          COMMAND "${GIT_EXECUTABLE}" -C
                  "${APSIS_DRIFT_TERMFORGE_SOURCE_DIR}"
                  merge-base --is-ancestor v0.42.0 HEAD
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
        "Ignoring TermForge source checkout older than v0.42.0: "
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

find_package(nlohmann_json 3.12.0 CONFIG QUIET)
if (NOT TARGET nlohmann_json::nlohmann_json)
  set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
  set(JSON_Install OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH
      SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
  )
  FetchContent_MakeAvailable(nlohmann_json)
endif ()

add_library(apsis_drift_lib STATIC
  src/benchmark.cpp
  src/celestial.cpp
  src/cockpit.cpp
  src/coordinates.cpp
  src/flight_deck_acceptance.cpp
  src/flight_input.cpp
  src/intersystem_contract.cpp
  src/intersystem_contract_acceptance.cpp
  src/intersystem_jump.cpp
  src/intersystem_jump_acceptance.cpp
  src/intersystem_planetfall.cpp
  src/intersystem_planetfall_acceptance.cpp
  src/intersystem_return_acceptance.cpp
  src/landscape.cpp
  src/local_system.cpp
  src/menu.cpp
  src/mission_board.cpp
  src/orbital.cpp
  src/origin_station.cpp
  src/origin_return.cpp
  src/planet.cpp
  src/planetfall_acceptance.cpp
  src/planetary_flight.cpp
  src/planetary_presentation.cpp
  src/render_profile.cpp
  src/save_file.cpp
  src/save_schema.cpp
  src/seed.cpp
  src/signal_collection.cpp
  src/signal_navigation_acceptance.cpp
  src/signal_run.cpp
  src/signal_run_acceptance.cpp
  src/signal_input.cpp
  src/signal_scanner.cpp
  src/simulation.cpp
  src/surface_signals.cpp
  src/system_flight.cpp
  src/system_flight_acceptance.cpp
  src/system_rendering.cpp
  src/terrain_tiles.cpp
  src/title.cpp
  src/world_delta_journal.cpp
)
add_library(apsis-drift::lib ALIAS apsis_drift_lib)

target_include_directories(apsis_drift_lib
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_features(apsis_drift_lib PUBLIC cxx_std_23)
target_link_libraries(apsis_drift_lib
  PUBLIC termforge::lib
  PRIVATE nlohmann_json::nlohmann_json
)

add_executable(apsis-drift
  src/main.cpp
)
target_link_libraries(apsis-drift PRIVATE apsis-drift::lib)

if (APSIS_DRIFT_TESTS AND BUILD_TESTING)
  add_executable(apsis-drift-tests test/test.cpp)
  target_link_libraries(apsis-drift-tests PRIVATE apsis-drift::lib)
  target_include_directories(apsis-drift-tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_definitions(apsis-drift-tests PRIVATE
    APSIS_DRIFT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME apsis-drift-tests COMMAND apsis-drift-tests)
  add_test(NAME version-report COMMAND apsis-drift --version)
  set_tests_properties(version-report PROPERTIES
    PASS_REGULAR_EXPRESSION "apsis-drift ${PROJECT_VERSION}")

  add_test(NAME benchmark-profile-remote
    COMMAND apsis-drift --benchmark 1 --profile remote)
  add_test(NAME benchmark-profile-balanced
    COMMAND apsis-drift --benchmark 1 --profile balanced)
  add_test(NAME benchmark-viewport-640x360
    COMMAND apsis-drift --benchmark 1 --viewport 640x360)
  add_test(NAME benchmark-profile-local
    COMMAND apsis-drift --benchmark 1 --profile local)
  add_test(NAME benchmark-orbital-remote
    COMMAND apsis-drift --benchmark 1 --profile remote --workload orbital)
  add_test(NAME benchmark-orbital-local
    COMMAND apsis-drift --benchmark 1 --profile local --workload orbital)
  add_test(NAME benchmark-system-remote
    COMMAND apsis-drift --benchmark 1 --profile remote --workload system)
  add_test(NAME benchmark-system-local
    COMMAND apsis-drift --benchmark 1 --profile local --workload system)

  add_test(NAME system-navigation-acceptance-matrix
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_system_navigation_acceptance.cmake")
  add_test(NAME intersystem-jump-acceptance-matrix
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_intersystem_jump_acceptance.cmake")
  add_test(NAME system-flight-acceptance-matrix
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_system_flight_acceptance.cmake")
  add_test(NAME intersystem-planetfall-acceptance-matrix
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_intersystem_planetfall_acceptance.cmake")
  add_test(NAME intersystem-return-acceptance-matrix
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_intersystem_return_acceptance.cmake")
  add_test(NAME intersystem-contract-acceptance-matrix
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_intersystem_contract_acceptance.cmake")
  add_test(NAME benchmark-planetary-remote
    COMMAND apsis-drift --benchmark 4 --profile remote --workload planetary)
  add_test(NAME benchmark-planetary-local
    COMMAND apsis-drift --benchmark 4 --profile local --workload planetary)
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

  add_test(NAME planetfall-acceptance-matrix
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_planetfall_acceptance.cmake")

  add_test(NAME signal-collection-acceptance-matrix
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_signal_navigation_acceptance.cmake")

  add_test(NAME signal-run-acceptance-matrix
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_signal_run_acceptance.cmake")

  add_test(NAME reject-viewport-zero
    COMMAND apsis-drift --benchmark 1 --viewport 0x480)
  add_test(NAME reject-system-flight-missing-report
    COMMAND apsis-drift --system-flight-acceptance --driver kitty)
  set_tests_properties(reject-system-flight-missing-report
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME reject-intersystem-planetfall-missing-report
    COMMAND apsis-drift --intersystem-planetfall-acceptance --driver kitty)
  set_tests_properties(reject-intersystem-planetfall-missing-report
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME reject-intersystem-planetfall-missing-driver
    COMMAND apsis-drift --intersystem-planetfall-acceptance
            --report intersystem-planetfall-invalid.json)
  set_tests_properties(reject-intersystem-planetfall-missing-driver
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME reject-intersystem-planetfall-mode-conflict
    COMMAND apsis-drift --intersystem-planetfall-acceptance --driver ansi
            --benchmark 1 --report intersystem-planetfall-invalid.json)
  set_tests_properties(reject-intersystem-planetfall-mode-conflict
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME reject-intersystem-return-missing-report
    COMMAND apsis-drift --intersystem-return-acceptance --driver kitty)
  set_tests_properties(reject-intersystem-return-missing-report
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME reject-intersystem-return-missing-driver
    COMMAND apsis-drift --intersystem-return-acceptance
            --report intersystem-return-invalid.json)
  set_tests_properties(reject-intersystem-return-missing-driver
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME reject-intersystem-return-mode-conflict
    COMMAND apsis-drift --intersystem-return-acceptance --driver ansi
            --benchmark 1 --report intersystem-return-invalid.json)
  set_tests_properties(reject-intersystem-return-mode-conflict
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME reject-intersystem-contract-missing-report
    COMMAND apsis-drift --intersystem-contract-acceptance --driver kitty)
  set_tests_properties(reject-intersystem-contract-missing-report
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME reject-intersystem-contract-missing-driver
    COMMAND apsis-drift --intersystem-contract-acceptance
            --report intersystem-contract-invalid.json)
  set_tests_properties(reject-intersystem-contract-missing-driver
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME reject-intersystem-contract-mode-conflict
    COMMAND apsis-drift --intersystem-contract-acceptance --driver ansi
            --benchmark 1 --report intersystem-contract-invalid.json)
  set_tests_properties(reject-intersystem-contract-mode-conflict
    PROPERTIES WILL_FAIL TRUE)
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
  add_test(NAME reject-workload-without-mode
    COMMAND apsis-drift --workload orbital)
  add_test(NAME reject-profile-options-with-benchmark
    COMMAND apsis-drift --benchmark 1 --new-game-seed 0)
  add_test(NAME reject-load-and-new-game
    COMMAND apsis-drift --load profile.json --new-game-seed 1)
  add_test(NAME reject-profile-seed-alias
    COMMAND apsis-drift --save profile.json --seed 1)
  add_test(NAME reject-negative-new-game-seed
    COMMAND apsis-drift --new-game-seed -1)
  add_test(NAME reject-missing-load
    COMMAND apsis-drift --load definitely-missing-save.json)
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
  add_test(NAME reject-planetfall-missing-report
    COMMAND apsis-drift --planetfall-acceptance)
  add_test(NAME reject-planetfall-seed-override
    COMMAND apsis-drift --planetfall-acceptance --seed 42
            --report planetfall-invalid.json)
  add_test(NAME reject-planetfall-mode-conflict
    COMMAND apsis-drift --planetfall-acceptance --benchmark 1
            --report planetfall-invalid.json)
  add_test(NAME reject-planetfall-driver
    COMMAND apsis-drift --planetfall-acceptance --driver kitty
            --report planetfall-invalid.json)
  add_test(NAME reject-signal-navigation-missing-report
    COMMAND apsis-drift --signal-navigation-acceptance)
  add_test(NAME reject-signal-navigation-seed-override
    COMMAND apsis-drift --signal-navigation-acceptance --seed 42
            --report signal-navigation-invalid.json)
  add_test(NAME reject-signal-navigation-mode-conflict
    COMMAND apsis-drift --signal-navigation-acceptance --benchmark 1
            --report signal-navigation-invalid.json)
  add_test(NAME reject-signal-run-missing-report
    COMMAND apsis-drift --signal-run-acceptance --driver ansi)
  add_test(NAME reject-signal-run-missing-driver
    COMMAND apsis-drift --signal-run-acceptance
            --report signal-run-invalid.json)
  add_test(NAME reject-signal-run-seed-override
    COMMAND apsis-drift --signal-run-acceptance --driver kitty --seed 42
            --report signal-run-invalid.json)
  add_test(NAME reject-signal-run-mode-conflict
    COMMAND apsis-drift --signal-run-acceptance --driver ansi --benchmark 1
            --report signal-run-invalid.json)
  add_test(NAME reject-system-navigation-missing-report
    COMMAND apsis-drift --system-navigation-acceptance --driver ansi)
  add_test(NAME reject-system-navigation-missing-driver
    COMMAND apsis-drift --system-navigation-acceptance
            --report system-navigation-invalid.json)
  add_test(NAME reject-system-navigation-mode-conflict
    COMMAND apsis-drift --system-navigation-acceptance --driver kitty
            --benchmark 1 --report system-navigation-invalid.json)
  add_test(NAME reject-intersystem-jump-missing-report
    COMMAND apsis-drift --intersystem-jump-acceptance --driver ansi)
  add_test(NAME reject-intersystem-jump-missing-driver
    COMMAND apsis-drift --intersystem-jump-acceptance
            --report intersystem-jump-invalid.json)
  add_test(NAME reject-intersystem-jump-mode-conflict
    COMMAND apsis-drift --intersystem-jump-acceptance --driver kitty
            --benchmark 1 --report intersystem-jump-invalid.json)
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
    reject-workload-without-mode
    reject-profile-options-with-benchmark
    reject-load-and-new-game
    reject-profile-seed-alias
    reject-negative-new-game-seed
    reject-missing-load
    reject-keyboard-with-automatic-driver
    reject-acceptance-missing-report
    reject-acceptance-seed-override
    reject-acceptance-mode-conflict
    reject-planetfall-missing-report
    reject-planetfall-seed-override
    reject-planetfall-mode-conflict
    reject-planetfall-driver
    reject-signal-navigation-missing-report
    reject-signal-navigation-seed-override
    reject-signal-navigation-mode-conflict
    reject-signal-run-missing-report
    reject-signal-run-missing-driver
    reject-signal-run-seed-override
    reject-signal-run-mode-conflict
    reject-system-navigation-missing-report
    reject-system-navigation-missing-driver
    reject-system-navigation-mode-conflict
    reject-intersystem-jump-missing-report
    reject-intersystem-jump-missing-driver
    reject-intersystem-jump-mode-conflict
    PROPERTIES WILL_FAIL TRUE
  )
endif ()
