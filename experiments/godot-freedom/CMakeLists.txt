add_executable(apsis-drift-godot-snapshot snapshot_main.cpp)
target_link_libraries(apsis-drift-godot-snapshot PRIVATE
  apsis-drift::lib nlohmann_json::nlohmann_json)
target_compile_options(apsis-drift-godot-snapshot PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)

if (APSIS_DRIFT_TESTS AND BUILD_TESTING)
  add_executable(apsis-drift-surface-start-tests surface_start_test.cpp)
  target_link_libraries(apsis-drift-surface-start-tests PRIVATE apsis-drift::lib nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-surface-start-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME godot-surface-start-contract COMMAND apsis-drift-surface-start-tests)
  set_tests_properties(godot-surface-start-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-flight-journey-tests flight_journey_test.cpp)
  target_link_libraries(apsis-drift-flight-journey-tests PRIVATE apsis-drift::lib)
  target_compile_options(apsis-drift-flight-journey-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME godot-flight-journey-contract COMMAND apsis-drift-flight-journey-tests)
  add_executable(apsis-drift-thrust-flight-tests thrust_flight_test.cpp)
  target_link_libraries(apsis-drift-thrust-flight-tests PRIVATE apsis-drift::lib)
  target_compile_options(apsis-drift-thrust-flight-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME godot-thrust-flight-contract COMMAND apsis-drift-thrust-flight-tests)
  add_executable(apsis-drift-analog-input-tests analog_test.cpp)
  target_link_libraries(apsis-drift-analog-input-tests PRIVATE apsis-drift::lib)
  target_compile_options(apsis-drift-analog-input-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME godot-analog-input-contract COMMAND apsis-drift-analog-input-tests)
  add_executable(apsis-drift-godot-snapshot-tests snapshot_test.cpp)
  target_link_libraries(apsis-drift-godot-snapshot-tests PRIVATE
    apsis-drift::lib nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-godot-snapshot-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME godot-snapshot-contract COMMAND apsis-drift-godot-snapshot-tests)
  find_package(Threads REQUIRED)
  add_executable(apsis-drift-planet-stream-tests streaming_test.cpp)
  target_link_libraries(apsis-drift-planet-stream-tests PRIVATE apsis-drift::lib
    nlohmann_json::nlohmann_json Threads::Threads)
  target_compile_options(apsis-drift-planet-stream-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME godot-planet-stream-contract COMMAND apsis-drift-planet-stream-tests)
  set_tests_properties(godot-planet-stream-contract PROPERTIES TIMEOUT 90)
endif ()

if (APSIS_DRIFT_GODOT_LIVE)
  # A minimal RefCounted binding, pinned to a stable backwards-compatible API.
  set(GODOTCPP_BUILD_PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/binding-profile.json"
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
    apsis-drift::lib godot::cpp nlohmann_json::nlohmann_json Threads::Threads)
  target_compile_options(apsis_freedom_bridge PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>)
  set_target_properties(apsis_freedom_bridge PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
  add_custom_command(TARGET apsis_freedom_bridge POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy
      "$<TARGET_FILE:apsis_freedom_bridge>"
      "${CMAKE_CURRENT_SOURCE_DIR}/bin/$<TARGET_FILE_NAME:apsis_freedom_bridge>.${CMAKE_CXX_COMPILER_ID}.new"
    COMMAND "${CMAKE_COMMAND}" -E rename
      "${CMAKE_CURRENT_SOURCE_DIR}/bin/$<TARGET_FILE_NAME:apsis_freedom_bridge>.${CMAKE_CXX_COMPILER_ID}.new"
      "${CMAKE_CURRENT_SOURCE_DIR}/bin/$<TARGET_FILE_NAME:apsis_freedom_bridge>")
  configure_file(freedom.gdextension.in
    "${CMAKE_CURRENT_SOURCE_DIR}/bin/freedom.gdextension" COPYONLY)
  configure_file("${godot_cpp_SOURCE_DIR}/LICENSE.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/bin/GODOT_CPP_LICENSE.md" COPYONLY)
endif ()
