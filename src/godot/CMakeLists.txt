add_executable(apsis-drift-godot-snapshot snapshot_main.cpp)
target_link_libraries(apsis-drift-godot-snapshot PRIVATE
  apsis-drift::core nlohmann_json::nlohmann_json)
target_compile_options(apsis-drift-godot-snapshot PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)

if (APSIS_DRIFT_TESTS AND BUILD_TESTING)
  # Opt-in composed voyage exporter. Its conservative pilot is test code only;
  # consumers render the actual phase saves rather than relocated practice starts.
  add_executable(apsis-drift-freedom-planetary-fixture "${PROJECT_SOURCE_DIR}/test/godot/freedom_planetary_fixture.cpp")
  target_link_libraries(apsis-drift-freedom-planetary-fixture PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-freedom-planetary-fixture PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_executable(apsis-drift-freedom-start-fixture "${PROJECT_SOURCE_DIR}/test/godot/freedom_start_fixture.cpp")
  target_link_libraries(apsis-drift-freedom-start-fixture PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-freedom-start-fixture PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_executable(apsis-drift-orbit-assist-tests "${PROJECT_SOURCE_DIR}/test/godot/orbit_assist_test.cpp")
  target_link_libraries(apsis-drift-orbit-assist-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-orbit-assist-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME godot-orbit-assist-contract COMMAND apsis-drift-orbit-assist-tests)
  set_tests_properties(godot-orbit-assist-contract PROPERTIES TIMEOUT 60)
  add_executable(apsis-drift-flight-guidance-tests "${PROJECT_SOURCE_DIR}/test/godot/flight_guidance_test.cpp")
  target_link_libraries(apsis-drift-flight-guidance-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-flight-guidance-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME godot-flight-guidance-contract COMMAND apsis-drift-flight-guidance-tests)
  set_tests_properties(godot-flight-guidance-contract PROPERTIES TIMEOUT 30)
  add_executable(apsis-drift-surface-start-tests "${PROJECT_SOURCE_DIR}/test/godot/surface_start_test.cpp")
  target_link_libraries(apsis-drift-surface-start-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-surface-start-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME godot-surface-start-contract COMMAND apsis-drift-surface-start-tests)
  set_tests_properties(godot-surface-start-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-flight-journey-tests "${PROJECT_SOURCE_DIR}/test/godot/flight_journey_test.cpp")
  target_link_libraries(apsis-drift-flight-journey-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-flight-journey-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME godot-flight-journey-contract COMMAND apsis-drift-flight-journey-tests)
  add_executable(apsis-drift-thrust-flight-tests "${PROJECT_SOURCE_DIR}/test/godot/thrust_flight_test.cpp")
  target_link_libraries(apsis-drift-thrust-flight-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-thrust-flight-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME godot-thrust-flight-contract COMMAND apsis-drift-thrust-flight-tests)
  add_executable(apsis-drift-analog-input-tests "${PROJECT_SOURCE_DIR}/test/godot/analog_test.cpp")
  target_link_libraries(apsis-drift-analog-input-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-analog-input-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME godot-analog-input-contract COMMAND apsis-drift-analog-input-tests)
  add_executable(apsis-drift-godot-snapshot-tests "${PROJECT_SOURCE_DIR}/test/godot/snapshot_test.cpp")
  target_link_libraries(apsis-drift-godot-snapshot-tests PRIVATE
    apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-godot-snapshot-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME godot-snapshot-contract COMMAND apsis-drift-godot-snapshot-tests)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if (Python3_Interpreter_FOUND)
    add_test(NAME godot-physical-snapshot-cli-contract
      COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/test/godot/snapshot_cli_test.py"
        "$<TARGET_FILE:apsis-drift-godot-snapshot>")
    set_tests_properties(godot-physical-snapshot-cli-contract PROPERTIES TIMEOUT 90)
  endif ()
  find_package(Threads REQUIRED)
  add_executable(apsis-drift-contact-surface-tests
    contact_surface.cpp "${PROJECT_SOURCE_DIR}/test/godot/contact_surface_test.cpp")
  target_link_libraries(apsis-drift-contact-surface-tests PRIVATE
    apsis-drift::core nlohmann_json::nlohmann_json Threads::Threads)
  target_compile_options(apsis-drift-contact-surface-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME godot-contact-surface-contract COMMAND apsis-drift-contact-surface-tests)
  set_tests_properties(godot-contact-surface-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-contact-patch-tests
    contact_surface.cpp contact_patch.cpp "${PROJECT_SOURCE_DIR}/test/godot/contact_patch_test.cpp")
  target_link_libraries(apsis-drift-contact-patch-tests PRIVATE
    apsis-drift::core nlohmann_json::nlohmann_json Threads::Threads)
  target_compile_options(apsis-drift-contact-patch-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME godot-contact-patch-contract COMMAND apsis-drift-contact-patch-tests)
  set_tests_properties(godot-contact-patch-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-saved-contact-geometry-tests
    contact_surface.cpp contact_patch.cpp saved_contact_geometry.cpp
    "${PROJECT_SOURCE_DIR}/test/godot/saved_contact_geometry_test.cpp")
  target_include_directories(apsis-drift-saved-contact-geometry-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(apsis-drift-saved-contact-geometry-tests PRIVATE
    apsis-drift::core nlohmann_json::nlohmann_json Threads::Threads)
  target_compile_options(apsis-drift-saved-contact-geometry-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME godot-saved-contact-geometry-contract
    COMMAND apsis-drift-saved-contact-geometry-tests)
  set_tests_properties(godot-saved-contact-geometry-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-planet-stream-tests "${PROJECT_SOURCE_DIR}/test/godot/streaming_test.cpp")
  target_link_libraries(apsis-drift-planet-stream-tests PRIVATE apsis-drift::core
    nlohmann_json::nlohmann_json Threads::Threads)
  target_compile_options(apsis-drift-planet-stream-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME godot-planet-stream-contract COMMAND apsis-drift-planet-stream-tests)
  set_tests_properties(godot-planet-stream-contract PROPERTIES TIMEOUT 90)
endif ()

if (APSIS_DRIFT_GODOT_LIVE)
  # A minimal RefCounted binding, pinned to a stable backwards-compatible API.
  set(GODOTCPP_BUILD_PROFILE "${PROJECT_SOURCE_DIR}/godot/binding-profile.json"
      CACHE PATH "Godot binding profile" FORCE)
  set(GODOTCPP_DISABLE_EXCEPTIONS OFF CACHE BOOL "Allow C++ boundary error handling" FORCE)
  FetchContent_Declare(godot_cpp
    URL https://github.com/godotengine/godot-cpp/archive/e83fd0904c13356ed1d4c3d09f8bb9132bdc6b77.tar.gz
    URL_HASH SHA256=d4c03daf46c8bef4544614182b0762c3e32789151c7f8f44e8eb2053d5cddeba
    EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(godot_cpp)
  set_property(TARGET godot-cpp PROPERTY SYSTEM TRUE)
  add_library(apsis_freedom_bridge SHARED live_bridge.cpp)
  find_package(Threads REQUIRED)
  target_link_libraries(apsis_freedom_bridge PRIVATE
    apsis-drift::core godot::cpp nlohmann_json::nlohmann_json Threads::Threads)
  target_compile_options(apsis_freedom_bridge PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>)
  set_target_properties(apsis_freedom_bridge PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
  add_custom_command(TARGET apsis_freedom_bridge POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy
      "$<TARGET_FILE:apsis_freedom_bridge>"
      "${PROJECT_SOURCE_DIR}/godot/bin/$<TARGET_FILE_NAME:apsis_freedom_bridge>.${CMAKE_CXX_COMPILER_ID}.new"
    COMMAND "${CMAKE_COMMAND}" -E rename
      "${PROJECT_SOURCE_DIR}/godot/bin/$<TARGET_FILE_NAME:apsis_freedom_bridge>.${CMAKE_CXX_COMPILER_ID}.new"
      "${PROJECT_SOURCE_DIR}/godot/bin/$<TARGET_FILE_NAME:apsis_freedom_bridge>")
  configure_file("${PROJECT_SOURCE_DIR}/godot/freedom.gdextension.in"
    "${PROJECT_SOURCE_DIR}/godot/bin/freedom.gdextension" COPYONLY)
  configure_file("${godot_cpp_SOURCE_DIR}/LICENSE.md"
    "${PROJECT_SOURCE_DIR}/godot/bin/GODOT_CPP_LICENSE.md" COPYONLY)
endif ()
