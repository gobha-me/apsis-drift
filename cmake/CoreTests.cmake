if (APSIS_DRIFT_TESTS AND BUILD_TESTING)
  add_executable(apsis-drift-charted-arrival-targeting-tests
    test/charted_arrival_targeting_test.cpp)
  target_link_libraries(apsis-drift-charted-arrival-targeting-tests PRIVATE
    apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-charted-arrival-targeting-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME charted-arrival-targeting-contract
    COMMAND apsis-drift-charted-arrival-targeting-tests)
  set_tests_properties(charted-arrival-targeting-contract PROPERTIES TIMEOUT 120)
  add_executable(apsis-drift-charted-arrival-save-tests
    test/charted_arrival_save_test.cpp)
  target_include_directories(apsis-drift-charted-arrival-save-tests PRIVATE src)
  target_link_libraries(apsis-drift-charted-arrival-save-tests PRIVATE
    apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-charted-arrival-save-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME charted-arrival-save-contract
    COMMAND apsis-drift-charted-arrival-save-tests)
  set_tests_properties(charted-arrival-save-contract PROPERTIES TIMEOUT 120)

  foreach(service_case IN ITEMS origin-service-craft origin-service-save origin-service-sortie-save freedom-assistance-save native-origin-assistance)
    string(REPLACE "-" "_" service_source "${service_case}")
    add_executable(apsis-drift-${service_case}-tests test/${service_source}_test.cpp)
    target_include_directories(apsis-drift-${service_case}-tests PRIVATE src)
    target_link_libraries(apsis-drift-${service_case}-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
    target_compile_options(apsis-drift-${service_case}-tests PRIVATE
      $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
    if(service_case STREQUAL "origin-service-craft" OR service_case STREQUAL "origin-service-save")
      add_test(NAME ${service_case}-contract COMMAND apsis-drift-${service_case}-tests)
    elseif(service_case STREQUAL "native-origin-assistance")
      add_test(NAME ${service_case}-contract COMMAND apsis-drift-${service_case}-tests
        "${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures/freedom/origin-service-moon-landed.json"
        "${CMAKE_CURRENT_BINARY_DIR}/origin-service-native-test")
    else()
      add_test(NAME ${service_case}-contract COMMAND apsis-drift-${service_case}-tests
        "${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures/freedom/origin-service-moon-landed.json"
        "${CMAKE_CURRENT_BINARY_DIR}/${service_case}-report.json")
    endif()
    set_tests_properties(${service_case}-contract PROPERTIES TIMEOUT 180)
  endforeach()
  foreach(reuse_case IN ITEMS freedom-origin-local-travel-save native-local-route-reuse native-port-rendezvous native-port-docking native-local-choice origin-station-crew freedom-planet-jump freedom-composed-travel native-planet-travel freedom-jump-recovery freedom-jump-recovery-save native-jump-recovery freedom-jump-strain-save native-jump-strain foreign-moon-jump freedom-foreign-moon-save native-foreign-moon foreign-minor-jump freedom-foreign-minor-save native-foreign-minor)
    string(REPLACE "-" "_" reuse_source "${reuse_case}")
    add_executable(apsis-drift-${reuse_case}-tests test/${reuse_source}_test.cpp)
    target_include_directories(apsis-drift-${reuse_case}-tests PRIVATE src)
    target_link_libraries(apsis-drift-${reuse_case}-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
    target_compile_options(apsis-drift-${reuse_case}-tests PRIVATE
      $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
    add_test(NAME ${reuse_case}-contract COMMAND apsis-drift-${reuse_case}-tests)
    set_tests_properties(${reuse_case}-contract PROPERTIES TIMEOUT 180)
  endforeach()
  target_sources(apsis-drift-native-foreign-moon-tests PRIVATE
    test/native_foreign_moon_fixture.cpp test/native_foreign_moon_ground.cpp)
  target_sources(apsis-drift-native-foreign-minor-tests PRIVATE
    test/native_foreign_minor_fixture.cpp)
  foreach(exposure_case IN ITEMS moon-surface-landmarks native-moon-landmarks)
    string(REPLACE "-" "_" exposure_source "${exposure_case}")
    add_executable(apsis-drift-${exposure_case}-tests test/${exposure_source}_test.cpp)
    target_include_directories(apsis-drift-${exposure_case}-tests PRIVATE src)
    target_link_libraries(apsis-drift-${exposure_case}-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
    target_compile_options(apsis-drift-${exposure_case}-tests PRIVATE
      $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
    add_test(NAME ${exposure_case}-contract COMMAND apsis-drift-${exposure_case}-tests)
    set_tests_properties(${exposure_case}-contract PROPERTIES TIMEOUT 120)
  endforeach()
  foreach(minor_case IN ITEMS physical-minor rigid-minor minor-body-dynamics native-minor-jump)
    string(REPLACE "-" "_" minor_source "${minor_case}")
    add_executable(apsis-drift-${minor_case}-tests test/${minor_source}_test.cpp)
    target_include_directories(apsis-drift-${minor_case}-tests PRIVATE src)
    target_link_libraries(apsis-drift-${minor_case}-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
    target_compile_options(apsis-drift-${minor_case}-tests PRIVATE
      $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
    add_test(NAME ${minor_case}-contract COMMAND apsis-drift-${minor_case}-tests)
    set_tests_properties(${minor_case}-contract PROPERTIES TIMEOUT 120)
  endforeach()
  add_executable(apsis-drift-saved-minor-flight-tests test/godot/saved_minor_flight_test.cpp)
  target_include_directories(apsis-drift-saved-minor-flight-tests PRIVATE src)
  target_link_libraries(apsis-drift-saved-minor-flight-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-saved-minor-flight-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME saved-minor-flight-contract COMMAND apsis-drift-saved-minor-flight-tests)
  foreach(secondary_case IN ITEMS secondary-bodies secondary-observations physical-moon physical-moon-rotation moon-body-dynamics moon-terrain moon-surface-contact moon-landed-craft moon-surface-walker freedom-moon-flight-save freedom-local-jump moon-terrain-impact moon-retained-state freedom-local-travel-save native-local-jump)
    string(REPLACE "-" "_" secondary_source "${secondary_case}")
    add_executable(apsis-drift-${secondary_case}-tests test/${secondary_source}_test.cpp)
    target_include_directories(apsis-drift-${secondary_case}-tests PRIVATE src)
    target_link_libraries(apsis-drift-${secondary_case}-tests PRIVATE apsis-drift::core)
    target_compile_options(apsis-drift-${secondary_case}-tests PRIVATE
      $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
    add_test(NAME ${secondary_case}-contract COMMAND apsis-drift-${secondary_case}-tests)
    set_tests_properties(${secondary_case}-contract PROPERTIES TIMEOUT 120)
  endforeach()
  target_link_libraries(apsis-drift-freedom-moon-flight-save-tests PRIVATE nlohmann_json::nlohmann_json)
  target_link_libraries(apsis-drift-moon-retained-state-tests PRIVATE nlohmann_json::nlohmann_json)
  target_link_libraries(apsis-drift-freedom-local-travel-save-tests PRIVATE nlohmann_json::nlohmann_json)
  target_link_libraries(apsis-drift-native-local-jump-tests PRIVATE nlohmann_json::nlohmann_json)
  add_executable(apsis-drift-saved-moon-flight-tests test/godot/saved_moon_flight_test.cpp)
  target_include_directories(apsis-drift-saved-moon-flight-tests PRIVATE src)
  target_link_libraries(apsis-drift-saved-moon-flight-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-saved-moon-flight-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME saved-moon-flight-contract COMMAND apsis-drift-saved-moon-flight-tests)
  add_executable(apsis-drift-moon-streaming-tests test/godot/moon_streaming_test.cpp)
  target_link_libraries(apsis-drift-moon-streaming-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-moon-streaming-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME moon-streaming-contract COMMAND apsis-drift-moon-streaming-tests)

  add_executable(apsis-drift-surface-landmarks-tests test/surface_landmarks_test.cpp)
  target_link_libraries(apsis-drift-surface-landmarks-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-surface-landmarks-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME surface-landmarks-contract COMMAND apsis-drift-surface-landmarks-tests)
  set_tests_properties(surface-landmarks-contract PROPERTIES TIMEOUT 120)
  add_executable(apsis-drift-terrain-local-detail-tests
    test/terrain_local_detail_test.cpp)
  target_link_libraries(apsis-drift-terrain-local-detail-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-terrain-local-detail-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME terrain-local-detail-contract
    COMMAND apsis-drift-terrain-local-detail-tests)
  set_tests_properties(terrain-local-detail-contract PROPERTIES TIMEOUT 120)
  # Keep the deferred contracts on their optional library; the runtime tests
  # following this block continue to exercise apsis-drift::core directly.
  set(APSIS_BOARDING_RESEARCH_TEST_LIBRARY apsis-drift::boarding-research)
  add_executable(apsis-drift-boarding-route-intermediate-support01-tests
    test/origin_boarding_route_intermediate_support01_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-intermediate-support01-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-route-intermediate-support01-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-route-intermediate-support01-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-intermediate-support01-contract
    COMMAND apsis-drift-boarding-route-intermediate-support01-tests)
  set_tests_properties(boarding-route-intermediate-support01-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-boarding-route-intermediate-support-self01-tests
    test/origin_boarding_route_intermediate_support_self01_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-intermediate-support-self01-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-route-intermediate-support-self01-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-route-intermediate-support-self01-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-intermediate-support-self01-contract
    COMMAND apsis-drift-boarding-route-intermediate-support-self01-tests)
  set_tests_properties(boarding-route-intermediate-support-self01-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-boarding-route-intermediate-root-z01-tests
    test/origin_boarding_route_intermediate_root_z01_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-intermediate-root-z01-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-route-intermediate-root-z01-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-route-intermediate-root-z01-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-intermediate-root-z01-contract
    COMMAND apsis-drift-boarding-route-intermediate-root-z01-tests --regression)
  set_tests_properties(boarding-route-intermediate-root-z01-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-boarding-route-intermediate-hip-diagnostic01-tests
    test/origin_boarding_route_intermediate_hip_diagnostic01_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-intermediate-hip-diagnostic01-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-route-intermediate-hip-diagnostic01-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-route-intermediate-hip-diagnostic01-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-intermediate-hip-diagnostic01-contract
    COMMAND apsis-drift-boarding-route-intermediate-hip-diagnostic01-tests)
  set_tests_properties(boarding-route-intermediate-hip-diagnostic01-contract PROPERTIES TIMEOUT 180)

  add_executable(apsis-drift-boarding-route-intermediate-hip-diagnostic02-tests
    test/origin_boarding_route_intermediate_hip_diagnostic02_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-intermediate-hip-diagnostic02-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-route-intermediate-hip-diagnostic02-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-route-intermediate-hip-diagnostic02-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-intermediate-hip-diagnostic02-contract
    COMMAND apsis-drift-boarding-route-intermediate-hip-diagnostic02-tests)
  set_tests_properties(boarding-route-intermediate-hip-diagnostic02-contract PROPERTIES TIMEOUT 180)

  add_executable(apsis-drift-boarding-intermediate-endpoint01-tests
    test/origin_boarding_intermediate_endpoint01_test.cpp)
  target_link_libraries(apsis-drift-boarding-intermediate-endpoint01-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-intermediate-endpoint01-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-intermediate-endpoint01-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-intermediate-endpoint01-contract
    COMMAND apsis-drift-boarding-intermediate-endpoint01-tests)
  set_tests_properties(boarding-intermediate-endpoint01-contract PROPERTIES TIMEOUT 180)

  add_executable(apsis-drift-boarding-intermediate-reach-evidence01-tests
    test/origin_boarding_intermediate_reach_evidence01_test.cpp)
  target_link_libraries(apsis-drift-boarding-intermediate-reach-evidence01-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-intermediate-reach-evidence01-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-intermediate-reach-evidence01-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-intermediate-reach-evidence01-contract
    COMMAND apsis-drift-boarding-intermediate-reach-evidence01-tests)
  set_tests_properties(boarding-intermediate-reach-evidence01-contract PROPERTIES TIMEOUT 180)

  add_executable(apsis-drift-boarding-intermediate-endpoint02-tests
    test/origin_boarding_intermediate_endpoint02_test.cpp)
  target_link_libraries(apsis-drift-boarding-intermediate-endpoint02-tests
    PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_compile_options(apsis-drift-boarding-intermediate-endpoint02-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  target_include_directories(apsis-drift-boarding-intermediate-endpoint02-tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  add_test(NAME boarding-intermediate-endpoint02-contract
    COMMAND apsis-drift-boarding-intermediate-endpoint02-tests)
  set_tests_properties(boarding-intermediate-endpoint02-contract PROPERTIES TIMEOUT 180)

  add_executable(apsis-drift-boarding-intermediate-sector-evidence01-tests
    test/origin_boarding_intermediate_sector_evidence01_test.cpp)
  target_link_libraries(apsis-drift-boarding-intermediate-sector-evidence01-tests
    PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_compile_options(apsis-drift-boarding-intermediate-sector-evidence01-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  target_include_directories(apsis-drift-boarding-intermediate-sector-evidence01-tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  add_test(NAME boarding-intermediate-sector-evidence01-contract
    COMMAND apsis-drift-boarding-intermediate-sector-evidence01-tests)
  set_tests_properties(boarding-intermediate-sector-evidence01-contract PROPERTIES TIMEOUT 180)

  add_executable(apsis-drift-boarding-intermediate-endpoint03-tests
    test/origin_boarding_intermediate_endpoint03_test.cpp)
  target_link_libraries(apsis-drift-boarding-intermediate-endpoint03-tests
    PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_compile_options(apsis-drift-boarding-intermediate-endpoint03-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  target_include_directories(apsis-drift-boarding-intermediate-endpoint03-tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  add_test(NAME boarding-intermediate-endpoint03-contract
    COMMAND apsis-drift-boarding-intermediate-endpoint03-tests)
  set_tests_properties(boarding-intermediate-endpoint03-contract PROPERTIES TIMEOUT 180)

  add_executable(apsis-drift-boarding-intermediate-sector-evidence02-tests
    test/origin_boarding_intermediate_sector_evidence02_test.cpp)
  target_link_libraries(apsis-drift-boarding-intermediate-sector-evidence02-tests
    PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_compile_options(apsis-drift-boarding-intermediate-sector-evidence02-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  target_include_directories(apsis-drift-boarding-intermediate-sector-evidence02-tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  add_test(NAME boarding-intermediate-sector-evidence02-contract
    COMMAND apsis-drift-boarding-intermediate-sector-evidence02-tests)
  set_tests_properties(boarding-intermediate-sector-evidence02-contract PROPERTIES TIMEOUT 180)

  add_executable(apsis-drift-boarding-intermediate-endpoint04-tests
    test/origin_boarding_intermediate_endpoint04_test.cpp)
  target_link_libraries(apsis-drift-boarding-intermediate-endpoint04-tests
    PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_compile_options(apsis-drift-boarding-intermediate-endpoint04-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  target_include_directories(apsis-drift-boarding-intermediate-endpoint04-tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  add_test(NAME boarding-intermediate-endpoint04-contract
    COMMAND apsis-drift-boarding-intermediate-endpoint04-tests)
  set_tests_properties(boarding-intermediate-endpoint04-contract PROPERTIES TIMEOUT 180)

  add_executable(apsis-drift-boarding-intermediate-endpoint05-tests
    test/origin_boarding_intermediate_endpoint05_test.cpp)
  target_link_libraries(apsis-drift-boarding-intermediate-endpoint05-tests
    PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_compile_options(apsis-drift-boarding-intermediate-endpoint05-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  target_include_directories(apsis-drift-boarding-intermediate-endpoint05-tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  add_test(NAME boarding-intermediate-endpoint05-contract
    COMMAND apsis-drift-boarding-intermediate-endpoint05-tests)
  set_tests_properties(boarding-intermediate-endpoint05-contract PROPERTIES TIMEOUT 180)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_tests_properties(boarding-intermediate-endpoint05-contract PROPERTIES
      ENVIRONMENT "LD_BIND_NOW=1;LC_ALL=C;LANG=C;GLIBC_TUNABLES=glibc.malloc.tcache_count=0")
    set_property(TEST boarding-intermediate-endpoint05-contract
      PROPERTY ENVIRONMENT_MODIFICATION
        "LD_PRELOAD=unset:"
        "LD_AUDIT=unset:"
        "LD_LIBRARY_PATH=unset:"
        "LD_PROFILE=unset:"
        "LD_DEBUG=unset:"
        "LD_DEBUG_OUTPUT=unset:"
        "LD_BIND_NOT=unset:"
        "LD_ORIGIN_PATH=unset:"
        "LD_ASSUME_KERNEL=unset:"
        "LD_HWCAP_MASK=unset:"
        "LD_TRACE_LOADED_OBJECTS=unset:"
        "LD_USE_LOAD_BIAS=unset:"
        "GLIBCXX_TUNABLES=unset:"
        "MALLOC_CHECK_=unset:"
        "MALLOC_PERTURB_=unset:"
        "MALLOC_ARENA_MAX=unset:"
        "MALLOC_ARENA_TEST=unset:"
        "MALLOC_MMAP_THRESHOLD_=unset:"
        "MALLOC_MMAP_MAX_=unset:"
        "MALLOC_TRIM_THRESHOLD_=unset:"
        "MALLOC_TOP_PAD_=unset:"
        "MALLOC_MXFAST_=unset:"
        "LD_DYNAMIC_WEAK=unset:"
        "LD_SHOW_AUXV=unset:"
        "LD_PROFILE_OUTPUT=unset:"
        "LD_VERBOSE=unset:"
        "LD_WARN=unset:"
    )
  endif()

  add_executable(apsis-drift-boarding-intermediate-endpoint06-tests
    test/origin_boarding_intermediate_endpoint06_test.cpp)
  target_link_libraries(apsis-drift-boarding-intermediate-endpoint06-tests
    PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_compile_options(apsis-drift-boarding-intermediate-endpoint06-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  target_include_directories(apsis-drift-boarding-intermediate-endpoint06-tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  add_test(NAME boarding-intermediate-endpoint06-contract
    COMMAND apsis-drift-boarding-intermediate-endpoint06-tests)
  set_tests_properties(boarding-intermediate-endpoint06-contract PROPERTIES TIMEOUT 180)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_tests_properties(boarding-intermediate-endpoint06-contract PROPERTIES
      ENVIRONMENT "LD_BIND_NOW=1;LC_ALL=C;LANG=C;GLIBC_TUNABLES=glibc.malloc.tcache_count=0")
    set_property(TEST boarding-intermediate-endpoint06-contract
      PROPERTY ENVIRONMENT_MODIFICATION
        "LD_PRELOAD=unset:"
        "LD_AUDIT=unset:"
        "LD_LIBRARY_PATH=unset:"
        "LD_PROFILE=unset:"
        "LD_DEBUG=unset:"
        "LD_DEBUG_OUTPUT=unset:"
        "LD_BIND_NOT=unset:"
        "LD_ORIGIN_PATH=unset:"
        "LD_ASSUME_KERNEL=unset:"
        "LD_HWCAP_MASK=unset:"
        "LD_TRACE_LOADED_OBJECTS=unset:"
        "LD_USE_LOAD_BIAS=unset:"
        "GLIBCXX_TUNABLES=unset:"
        "MALLOC_CHECK_=unset:"
        "MALLOC_PERTURB_=unset:"
        "MALLOC_ARENA_MAX=unset:"
        "MALLOC_ARENA_TEST=unset:"
        "MALLOC_MMAP_THRESHOLD_=unset:"
        "MALLOC_MMAP_MAX_=unset:"
        "MALLOC_TRIM_THRESHOLD_=unset:"
        "MALLOC_TOP_PAD_=unset:"
        "MALLOC_MXFAST_=unset:"
        "LD_DYNAMIC_WEAK=unset:"
        "LD_SHOW_AUXV=unset:"
        "LD_PROFILE_OUTPUT=unset:"
        "LD_VERBOSE=unset:"
        "LD_WARN=unset:"
    )
  endif()

  add_executable(apsis-drift-boarding-knee-compatibility-diagnostic01-tests
    test/origin_boarding_knee_compatibility_diagnostic01_test.cpp)
  target_link_libraries(apsis-drift-boarding-knee-compatibility-diagnostic01-tests
    PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_compile_options(apsis-drift-boarding-knee-compatibility-diagnostic01-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  target_include_directories(apsis-drift-boarding-knee-compatibility-diagnostic01-tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  add_test(NAME boarding-knee-compatibility-diagnostic01-contract
    COMMAND apsis-drift-boarding-knee-compatibility-diagnostic01-tests)
  set_tests_properties(boarding-knee-compatibility-diagnostic01-contract PROPERTIES TIMEOUT 180)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_tests_properties(boarding-knee-compatibility-diagnostic01-contract PROPERTIES
      ENVIRONMENT "LD_BIND_NOW=1;LC_ALL=C;LANG=C;GLIBC_TUNABLES=glibc.malloc.tcache_count=0")
    set_property(TEST boarding-knee-compatibility-diagnostic01-contract
      PROPERTY ENVIRONMENT_MODIFICATION
        "LD_PRELOAD=unset:"
        "LD_AUDIT=unset:"
        "LD_LIBRARY_PATH=unset:"
        "LD_PROFILE=unset:"
        "LD_DEBUG=unset:"
        "LD_DEBUG_OUTPUT=unset:"
        "LD_BIND_NOT=unset:"
        "LD_ORIGIN_PATH=unset:"
        "LD_ASSUME_KERNEL=unset:"
        "LD_HWCAP_MASK=unset:"
        "LD_TRACE_LOADED_OBJECTS=unset:"
        "LD_USE_LOAD_BIAS=unset:"
        "GLIBCXX_TUNABLES=unset:"
        "MALLOC_CHECK_=unset:"
        "MALLOC_PERTURB_=unset:"
        "MALLOC_ARENA_MAX=unset:"
        "MALLOC_ARENA_TEST=unset:"
        "MALLOC_MMAP_THRESHOLD_=unset:"
        "MALLOC_MMAP_MAX_=unset:"
        "MALLOC_TRIM_THRESHOLD_=unset:"
        "MALLOC_TOP_PAD_=unset:"
        "MALLOC_MXFAST_=unset:"
        "LD_DYNAMIC_WEAK=unset:"
        "LD_SHOW_AUXV=unset:"
        "LD_PROFILE_OUTPUT=unset:"
        "LD_VERBOSE=unset:"
        "LD_WARN=unset:"
    )
  endif()

  add_executable(apsis-drift-boarding-hip-joint-plane-method01-tests
    test/origin_boarding_hip_joint_plane_method01_test.cpp)
  target_link_libraries(apsis-drift-boarding-hip-joint-plane-method01-tests
    PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_compile_options(apsis-drift-boarding-hip-joint-plane-method01-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  target_include_directories(apsis-drift-boarding-hip-joint-plane-method01-tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  add_test(NAME boarding-hip-joint-plane-method01-contract
    COMMAND apsis-drift-boarding-hip-joint-plane-method01-tests)
  set_tests_properties(boarding-hip-joint-plane-method01-contract PROPERTIES TIMEOUT 180)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_tests_properties(boarding-hip-joint-plane-method01-contract PROPERTIES
      ENVIRONMENT "LD_BIND_NOW=1;LC_ALL=C;LANG=C;GLIBC_TUNABLES=glibc.malloc.tcache_count=0")
    set_property(TEST boarding-hip-joint-plane-method01-contract
      PROPERTY ENVIRONMENT_MODIFICATION
        "LD_PRELOAD=unset:"
        "LD_AUDIT=unset:"
        "LD_LIBRARY_PATH=unset:"
        "LD_PROFILE=unset:"
        "LD_DEBUG=unset:"
        "LD_DEBUG_OUTPUT=unset:"
        "LD_BIND_NOT=unset:"
        "LD_ORIGIN_PATH=unset:"
        "LD_ASSUME_KERNEL=unset:"
        "LD_HWCAP_MASK=unset:"
        "LD_TRACE_LOADED_OBJECTS=unset:"
        "LD_USE_LOAD_BIAS=unset:"
        "GLIBCXX_TUNABLES=unset:"
        "MALLOC_CHECK_=unset:"
        "MALLOC_PERTURB_=unset:"
        "MALLOC_ARENA_MAX=unset:"
        "MALLOC_ARENA_TEST=unset:"
        "MALLOC_MMAP_THRESHOLD_=unset:"
        "MALLOC_MMAP_MAX_=unset:"
        "MALLOC_TRIM_THRESHOLD_=unset:"
        "MALLOC_TOP_PAD_=unset:"
        "MALLOC_MXFAST_=unset:"
        "LD_DYNAMIC_WEAK=unset:"
        "LD_SHOW_AUXV=unset:"
        "LD_PROFILE_OUTPUT=unset:"
        "LD_VERBOSE=unset:"
        "LD_WARN=unset:"
    )
  endif()

  add_executable(apsis-drift-boarding-route-intermediate-unloaded01-tests
    test/origin_boarding_route_intermediate_unloaded01_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-intermediate-unloaded01-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-route-intermediate-unloaded01-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-route-intermediate-unloaded01-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-intermediate-unloaded01-contract
    COMMAND apsis-drift-boarding-route-intermediate-unloaded01-tests)
  set_tests_properties(boarding-route-intermediate-unloaded01-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-route-intermediate-load01-tests
    test/origin_boarding_route_intermediate_load01_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-intermediate-load01-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-route-intermediate-load01-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-route-intermediate-load01-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-intermediate-load01-contract
    COMMAND apsis-drift-boarding-route-intermediate-load01-tests)
  set_tests_properties(boarding-route-intermediate-load01-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-intermediate-pause-support02-tests
    test/origin_boarding_intermediate_pause_support02_test.cpp)
  target_link_libraries(apsis-drift-boarding-intermediate-pause-support02-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-intermediate-pause-support02-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-intermediate-pause-support02-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-intermediate-pause-support02-contract
    COMMAND apsis-drift-boarding-intermediate-pause-support02-tests)
  set_tests_properties(boarding-intermediate-pause-support02-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-intermediate-pause-envelope-tests
    test/origin_boarding_intermediate_pause_envelope_test.cpp)
  target_link_libraries(apsis-drift-boarding-intermediate-pause-envelope-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-intermediate-pause-envelope-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-intermediate-pause-envelope-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-intermediate-pause-envelope-contract
    COMMAND apsis-drift-boarding-intermediate-pause-envelope-tests)
  set_tests_properties(boarding-intermediate-pause-envelope-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-intermediate-pause-support-tests
    test/origin_boarding_intermediate_pause_support_test.cpp)
  target_link_libraries(apsis-drift-boarding-intermediate-pause-support-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-intermediate-pause-support-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-intermediate-pause-support-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-intermediate-pause-support-contract
    COMMAND apsis-drift-boarding-intermediate-pause-support-tests)
  set_tests_properties(boarding-intermediate-pause-support-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-route-intermediate-step02-tests
    test/origin_boarding_route_intermediate_step02_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-intermediate-step02-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-route-intermediate-step02-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-route-intermediate-step02-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-intermediate-step02-contract
    COMMAND apsis-drift-boarding-route-intermediate-step02-tests)
  set_tests_properties(boarding-route-intermediate-step02-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-route-intermediate-sector-tests
    test/origin_boarding_route_intermediate_step_attribution_driver.cpp
    test/origin_boarding_route_intermediate_step_attribution_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-intermediate-sector-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-route-intermediate-sector-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/test")
  target_compile_options(apsis-drift-boarding-route-intermediate-sector-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-intermediate-sector-attribution-contract
    COMMAND apsis-drift-boarding-route-intermediate-sector-tests)
  set_tests_properties(boarding-route-intermediate-sector-attribution-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-route-intermediate-step-tests test/origin_boarding_route_intermediate_step_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-intermediate-step-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-route-intermediate-step-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-route-intermediate-step-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-intermediate-step-contract COMMAND apsis-drift-boarding-route-intermediate-step-tests)
  set_tests_properties(boarding-route-intermediate-step-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-separation-ring-material-tests test/origin_boarding_separation_ring_material_test.cpp)
  target_link_libraries(apsis-drift-boarding-separation-ring-material-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-separation-ring-material-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-separation-ring-material-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-separation-ring-material-contract COMMAND apsis-drift-boarding-separation-ring-material-tests)
  set_tests_properties(boarding-separation-ring-material-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-route-checkpoint-world-material04-tests test/origin_boarding_route_checkpoint_world_material04_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-checkpoint-world-material04-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-route-checkpoint-world-material04-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-route-checkpoint-world-material04-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-checkpoint-world-material04-contract COMMAND apsis-drift-boarding-route-checkpoint-world-material04-tests)
  set_tests_properties(boarding-route-checkpoint-world-material04-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-hatch-seal-material-tests test/origin_boarding_hatch_seal_material_test.cpp)
  target_link_libraries(apsis-drift-boarding-hatch-seal-material-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-hatch-seal-material-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-hatch-seal-material-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-hatch-seal-material-contract COMMAND apsis-drift-boarding-hatch-seal-material-tests)
  set_tests_properties(boarding-hatch-seal-material-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-route-checkpoint-world-material03-tests test/origin_boarding_route_checkpoint_world_material03_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-checkpoint-world-material03-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-route-checkpoint-world-material03-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-route-checkpoint-world-material03-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-checkpoint-world-material03-contract COMMAND apsis-drift-boarding-route-checkpoint-world-material03-tests)
  set_tests_properties(boarding-route-checkpoint-world-material03-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-checkpoint-material-extension-tests test/origin_boarding_checkpoint_material_extension_test.cpp)
  target_link_libraries(apsis-drift-boarding-checkpoint-material-extension-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-checkpoint-material-extension-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-checkpoint-material-extension-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-checkpoint-material-extension-contract COMMAND apsis-drift-boarding-checkpoint-material-extension-tests)
  set_tests_properties(boarding-checkpoint-material-extension-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-route-checkpoint-world-material02-tests test/origin_boarding_route_checkpoint_world_material02_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-checkpoint-world-material02-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-route-checkpoint-world-material02-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-route-checkpoint-world-material02-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-checkpoint-world-material02-contract COMMAND apsis-drift-boarding-route-checkpoint-world-material02-tests)
  set_tests_properties(boarding-route-checkpoint-world-material02-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-route-checkpoint-world-material-tests test/origin_boarding_route_checkpoint_world_material_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-checkpoint-world-material-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-route-checkpoint-world-material-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-route-checkpoint-world-material-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-checkpoint-world-material-contract COMMAND apsis-drift-boarding-route-checkpoint-world-material-tests)
  set_tests_properties(boarding-route-checkpoint-world-material-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-route-checkpoint-unload-self02-tests test/origin_boarding_route_checkpoint_unload_self02_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-checkpoint-unload-self02-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-route-checkpoint-unload-self02-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-route-checkpoint-unload-self02-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-checkpoint-unload-self02-contract COMMAND apsis-drift-boarding-route-checkpoint-unload-self02-tests)
  set_tests_properties(boarding-route-checkpoint-unload-self02-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-route-checkpoint-unload-tests test/origin_boarding_route_checkpoint_unload_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-checkpoint-unload-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-route-checkpoint-unload-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-route-checkpoint-unload-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-checkpoint-unload-contract COMMAND apsis-drift-boarding-route-checkpoint-unload-tests)
  set_tests_properties(boarding-route-checkpoint-unload-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-route-port-unload-tests test/origin_boarding_route_port_unload_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-port-unload-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-route-port-unload-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-route-port-unload-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-port-unload-contract COMMAND apsis-drift-boarding-route-port-unload-tests)
  set_tests_properties(boarding-route-port-unload-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-initial-material-tests test/origin_boarding_initial_material_test.cpp)
  target_link_libraries(apsis-drift-boarding-initial-material-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-initial-material-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-initial-material-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-initial-material-contract COMMAND apsis-drift-boarding-initial-material-tests)
  set_tests_properties(boarding-initial-material-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-route-foot-phase-tests test/origin_boarding_route_foot_phase_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-foot-phase-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-route-foot-phase-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-route-foot-phase-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-foot-phase-contract COMMAND apsis-drift-boarding-route-foot-phase-tests)
  set_tests_properties(boarding-route-foot-phase-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-route-body-tests test/origin_boarding_route_body_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-body-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_compile_options(apsis-drift-boarding-route-body-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-body-contract COMMAND apsis-drift-boarding-route-body-tests)
  set_tests_properties(boarding-route-body-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-self-model03-tests test/origin_boarding_self_model03_test.cpp)
  target_link_libraries(apsis-drift-boarding-self-model03-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-self-model03-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-self-model03-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-self-model03-contract COMMAND apsis-drift-boarding-self-model03-tests)
  set_tests_properties(boarding-self-model03-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-boarding-self-model02-tests test/origin_boarding_self_model02_test.cpp)
  target_link_libraries(apsis-drift-boarding-self-model02-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-self-model02-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-self-model02-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-self-model02-contract COMMAND apsis-drift-boarding-self-model02-tests)
  set_tests_properties(boarding-self-model02-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-boarding-body-tests test/origin_boarding_body_test.cpp)
  target_link_libraries(apsis-drift-boarding-body-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-body-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-body-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-body-contract COMMAND apsis-drift-boarding-body-tests)
  set_tests_properties(boarding-body-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-boarding-lateral-body02-tests test/origin_boarding_lateral_body02_test.cpp)
  target_link_libraries(apsis-drift-boarding-lateral-body02-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_compile_options(apsis-drift-boarding-lateral-body02-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-lateral-body02-contract COMMAND apsis-drift-boarding-lateral-body02-tests)
  set_tests_properties(boarding-lateral-body02-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-boarding-self-model04-tests test/origin_boarding_self_model04_test.cpp)
  target_link_libraries(apsis-drift-boarding-self-model04-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_compile_options(apsis-drift-boarding-self-model04-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-self-model04-contract COMMAND apsis-drift-boarding-self-model04-tests)
  set_tests_properties(boarding-self-model04-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-boarding-boot-support-tests test/origin_boarding_boot_support_test.cpp)
  target_link_libraries(apsis-drift-boarding-boot-support-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_compile_options(apsis-drift-boarding-boot-support-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-boot-support-contract COMMAND apsis-drift-boarding-boot-support-tests)
  set_tests_properties(boarding-boot-support-contract PROPERTIES TIMEOUT 120)
  add_executable(apsis-drift-boarding-foot-sites-tests test/origin_boarding_foot_sites_test.cpp)
  target_link_libraries(apsis-drift-boarding-foot-sites-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-foot-sites-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-foot-sites-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-foot-sites-contract COMMAND apsis-drift-boarding-foot-sites-tests)
  set_tests_properties(boarding-foot-sites-contract PROPERTIES TIMEOUT 120)
  add_executable(apsis-drift-boarding-source-endpoint-tests test/origin_boarding_source_endpoint_test.cpp)
  target_link_libraries(apsis-drift-boarding-source-endpoint-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-source-endpoint-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-source-endpoint-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-source-endpoint-contract COMMAND apsis-drift-boarding-source-endpoint-tests)
  set_tests_properties(boarding-source-endpoint-contract PROPERTIES TIMEOUT 120)
  add_executable(apsis-drift-boarding-source-endpoint-self-tests test/origin_boarding_source_endpoint_self_test.cpp)
  target_link_libraries(apsis-drift-boarding-source-endpoint-self-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-source-endpoint-self-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-source-endpoint-self-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-source-endpoint-self-contract COMMAND apsis-drift-boarding-source-endpoint-self-tests)
  set_tests_properties(boarding-source-endpoint-self-contract PROPERTIES TIMEOUT 120)
  add_executable(apsis-drift-boarding-source-endpoint-load-tests test/origin_boarding_source_endpoint_load_test.cpp)
  target_link_libraries(apsis-drift-boarding-source-endpoint-load-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-source-endpoint-load-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-source-endpoint-load-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-source-endpoint-load-contract COMMAND apsis-drift-boarding-source-endpoint-load-tests)
  set_tests_properties(boarding-source-endpoint-load-contract PROPERTIES TIMEOUT 120)
  add_executable(apsis-drift-boarding-source-endpoint-surface-checkpoint-tests test/origin_boarding_source_endpoint_surface_checkpoint_test.cpp)
  target_link_libraries(apsis-drift-boarding-source-endpoint-surface-checkpoint-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-source-endpoint-surface-checkpoint-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-source-endpoint-surface-checkpoint-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-source-endpoint-surface-checkpoint-contract COMMAND apsis-drift-boarding-source-endpoint-surface-checkpoint-tests)
  set_tests_properties(boarding-source-endpoint-surface-checkpoint-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-boarding-lower-foot-transfer-tests test/origin_boarding_lower_foot_transfer_test.cpp)
  target_link_libraries(apsis-drift-boarding-lower-foot-transfer-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-lower-foot-transfer-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-lower-foot-transfer-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-lower-foot-transfer-contract COMMAND apsis-drift-boarding-lower-foot-transfer-tests)
  set_tests_properties(boarding-lower-foot-transfer-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-boarding-lower-foot-transfer-surface-sweep-tests test/origin_boarding_lower_foot_transfer_surface_sweep_test.cpp)
  target_link_libraries(apsis-drift-boarding-lower-foot-transfer-surface-sweep-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-lower-foot-transfer-surface-sweep-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-lower-foot-transfer-surface-sweep-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-lower-foot-transfer-surface-sweep-contract COMMAND apsis-drift-boarding-lower-foot-transfer-surface-sweep-tests)
  set_tests_properties(boarding-lower-foot-transfer-surface-sweep-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-boarding-world-checkpoint-tests test/origin_boarding_world_checkpoint_test.cpp)
  target_link_libraries(apsis-drift-boarding-world-checkpoint-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-world-checkpoint-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-world-checkpoint-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-world-checkpoint-contract COMMAND apsis-drift-boarding-world-checkpoint-tests)
  set_tests_properties(boarding-world-checkpoint-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-boarding-planted-legs-tests test/origin_boarding_planted_legs_test.cpp)
  target_link_libraries(apsis-drift-boarding-planted-legs-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-planted-legs-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-planted-legs-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-planted-legs-contract COMMAND apsis-drift-boarding-planted-legs-tests)
  set_tests_properties(boarding-planted-legs-contract PROPERTIES TIMEOUT 120)
  add_executable(apsis-drift-boarding-planted-leg-timing-tests test/origin_boarding_planted_leg_timing_test.cpp)
  target_link_libraries(apsis-drift-boarding-planted-leg-timing-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-planted-leg-timing-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-planted-leg-timing-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-planted-leg-timing-contract COMMAND apsis-drift-boarding-planted-leg-timing-tests)
  set_tests_properties(boarding-planted-leg-timing-contract PROPERTIES TIMEOUT 120)
  add_executable(apsis-drift-boarding-planted-body-tests test/origin_boarding_planted_body_test.cpp)
  target_link_libraries(apsis-drift-boarding-planted-body-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-planted-body-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-planted-body-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-planted-body-contract COMMAND apsis-drift-boarding-planted-body-tests)
  set_tests_properties(boarding-planted-body-contract PROPERTIES TIMEOUT 120)
  add_executable(apsis-drift-boarding-planted-hip-preflight-tests test/origin_boarding_planted_hip_preflight_test.cpp)
  target_link_libraries(apsis-drift-boarding-planted-hip-preflight-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY})
  target_include_directories(apsis-drift-boarding-planted-hip-preflight-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-planted-hip-preflight-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-planted-hip-preflight-contract COMMAND apsis-drift-boarding-planted-hip-preflight-tests)
  set_tests_properties(boarding-planted-hip-preflight-contract PROPERTIES TIMEOUT 120)
  add_executable(apsis-drift-cabin-corridor-tests test/origin_cabin_corridor_test.cpp)
  target_link_libraries(apsis-drift-cabin-corridor-tests PRIVATE ${APSIS_BOARDING_RESEARCH_TEST_LIBRARY} nlohmann_json::nlohmann_json)
  target_compile_definitions(apsis-drift-cabin-corridor-tests PRIVATE
    APSIS_LOWER_CONTACT_FIXTURE="${APSIS_LOWER_CONTACT_DATA}"
    APSIS_LOWER_CONTACT_POLICY_FIXTURE="${APSIS_LOWER_CONTACT_POLICY}"
    APSIS_CABIN_CONTACT_FIXTURE="${APSIS_CABIN_CONTACT_DATA}")
  target_compile_options(apsis-drift-cabin-corridor-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME cabin-corridor-contract COMMAND apsis-drift-cabin-corridor-tests)
  set_tests_properties(cabin-corridor-contract PROPERTIES TIMEOUT 180)
  unset(APSIS_BOARDING_RESEARCH_TEST_LIBRARY)

  add_executable(apsis-drift-lower-cockpit-contact-tests test/origin_lower_cockpit_contact_test.cpp)
  target_link_libraries(apsis-drift-lower-cockpit-contact-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_definitions(apsis-drift-lower-cockpit-contact-tests PRIVATE
    APSIS_LOWER_CONTACT_FIXTURE="${APSIS_LOWER_CONTACT_DATA}"
    APSIS_LOWER_CONTACT_POLICY_FIXTURE="${APSIS_LOWER_CONTACT_POLICY}"
    APSIS_CABIN_CONTACT_FIXTURE="${APSIS_CABIN_CONTACT_DATA}")
  target_compile_options(apsis-drift-lower-cockpit-contact-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME lower-cockpit-contact-contract COMMAND apsis-drift-lower-cockpit-contact-tests)
  set_tests_properties(lower-cockpit-contact-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-stowed-lower-cockpit-contact-tests test/origin_stowed_lower_cockpit_contact_test.cpp)
  # This runtime-contact regression also invokes the deferred corridor certifier.
  target_link_libraries(apsis-drift-stowed-lower-cockpit-contact-tests PRIVATE apsis-drift::boarding-research nlohmann_json::nlohmann_json)
  target_compile_definitions(apsis-drift-stowed-lower-cockpit-contact-tests PRIVATE
    APSIS_LOWER_CONTACT_FIXTURE="${APSIS_LOWER_CONTACT_DATA}"
    APSIS_LOWER_CONTACT_POLICY_FIXTURE="${APSIS_LOWER_CONTACT_POLICY}"
    APSIS_CABIN_CONTACT_FIXTURE="${APSIS_CABIN_CONTACT_DATA}")
  target_compile_options(apsis-drift-stowed-lower-cockpit-contact-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME stowed-lower-cockpit-contact-contract COMMAND apsis-drift-stowed-lower-cockpit-contact-tests)
  set_tests_properties(stowed-lower-cockpit-contact-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-cabin-seam-tests test/origin_cabin_seam_test.cpp)
  target_link_libraries(apsis-drift-cabin-seam-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_definitions(apsis-drift-cabin-seam-tests PRIVATE
    APSIS_CABIN_CONTACT_FIXTURE="${APSIS_CABIN_CONTACT_DATA}")
  target_compile_options(apsis-drift-cabin-seam-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME cabin-seam-contract COMMAND apsis-drift-cabin-seam-tests)
  set_tests_properties(cabin-seam-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-boarding-support-tests test/origin_boarding_support_test.cpp)
  target_link_libraries(apsis-drift-boarding-support-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-boarding-support-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-support-contract COMMAND apsis-drift-boarding-support-tests)
  set_tests_properties(boarding-support-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-stowed-contact-partition-tests test/origin_stowed_contact_partition_test.cpp)
  target_link_libraries(apsis-drift-stowed-contact-partition-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-stowed-contact-partition-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME stowed-contact-partition-contract COMMAND apsis-drift-stowed-contact-partition-tests)
  set_tests_properties(stowed-contact-partition-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-operating-motion-tests test/operating_motion_test.cpp)
  target_link_libraries(apsis-drift-operating-motion-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_definitions(apsis-drift-operating-motion-tests PRIVATE
    APSIS_OPERATING_MOTION_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/native/operating-motion-01/sources")
  target_compile_options(apsis-drift-operating-motion-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME operating-motion-contract COMMAND apsis-drift-operating-motion-tests)
  set_tests_properties(operating-motion-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-origin-walker-tests test/origin_walker_test.cpp)
  target_link_libraries(apsis-drift-origin-walker-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-origin-walker-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
  add_test(NAME origin-walker-contract COMMAND apsis-drift-origin-walker-tests)
  set_tests_properties(origin-walker-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-freedom-journey-save-tests test/freedom_journey_save_test.cpp)
  target_link_libraries(apsis-drift-freedom-journey-save-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-freedom-journey-save-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
  add_test(NAME freedom-journey-save-contract COMMAND apsis-drift-freedom-journey-save-tests)
  set_tests_properties(freedom-journey-save-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-starting-assembly-save-tests test/freedom_starting_assembly_save_test.cpp)
  target_link_libraries(apsis-drift-starting-assembly-save-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-starting-assembly-save-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME starting-assembly-save-contract COMMAND apsis-drift-starting-assembly-save-tests)
  set_tests_properties(starting-assembly-save-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-gameplay-boarding-tests test/origin_gameplay_boarding_test.cpp)
  target_link_libraries(apsis-drift-gameplay-boarding-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-gameplay-boarding-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME gameplay-boarding-contract COMMAND apsis-drift-gameplay-boarding-tests)
  set_tests_properties(gameplay-boarding-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-boarding-save-tests test/freedom_boarding_save_test.cpp)
  target_link_libraries(apsis-drift-boarding-save-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-boarding-save-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-save-contract COMMAND apsis-drift-boarding-save-tests)
  set_tests_properties(boarding-save-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-freedom-docking-save-tests test/freedom_docking_save_test.cpp)
  target_link_libraries(apsis-drift-freedom-docking-save-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-freedom-docking-save-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
  add_test(NAME freedom-docking-save-contract COMMAND apsis-drift-freedom-docking-save-tests)
  set_tests_properties(freedom-docking-save-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-native-startup-tests test/native_startup_test.cpp)
  target_link_libraries(apsis-drift-native-startup-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-native-startup-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME native-startup-contract COMMAND apsis-drift-native-startup-tests)
  add_executable(apsis-drift-freedom-recovery-tests test/freedom_recovery_test.cpp)
  target_link_libraries(apsis-drift-freedom-recovery-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-freedom-recovery-tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror -ffp-contract=off)
  add_test(NAME freedom-recovery-contract COMMAND apsis-drift-freedom-recovery-tests)

  add_executable(apsis-drift-freedom-flight-save-tests test/freedom_flight_save_test.cpp)
  target_link_libraries(apsis-drift-freedom-flight-save-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-freedom-flight-save-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
  add_test(NAME freedom-flight-save-contract COMMAND apsis-drift-freedom-flight-save-tests)
  set_tests_properties(freedom-flight-save-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-freedom-flight-import-tests test/freedom_flight_import_test.cpp)
  target_link_libraries(apsis-drift-freedom-flight-import-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-freedom-flight-import-tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -ffp-contract=off)
  add_test(NAME freedom-flight-import-contract COMMAND apsis-drift-freedom-flight-import-tests)
  set_tests_properties(freedom-flight-import-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-native-flight-session-tests test/native_flight_session_test.cpp)
  target_link_libraries(apsis-drift-native-flight-session-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-native-flight-session-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
  add_test(NAME native-flight-session-contract COMMAND apsis-drift-native-flight-session-tests)
  set_tests_properties(native-flight-session-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-native-system-release-tests test/native_system_release_test.cpp)
  target_link_libraries(apsis-drift-native-system-release-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-native-system-release-tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -ffp-contract=off)
  add_test(NAME native-system-release-contract COMMAND apsis-drift-native-system-release-tests)
  set_tests_properties(native-system-release-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-system-flight-validation-tests test/system_flight_validation_test.cpp)
  target_link_libraries(apsis-drift-system-flight-validation-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-system-flight-validation-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
  add_test(NAME system-flight-validation-contract COMMAND apsis-drift-system-flight-validation-tests)
  set_tests_properties(system-flight-validation-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-station-geometry-tests test/station_geometry_test.cpp)
  target_link_libraries(apsis-drift-station-geometry-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-station-geometry-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
  add_test(NAME station-geometry-contract COMMAND apsis-drift-station-geometry-tests)
  add_executable(apsis-drift-craft-frame-tests test/craft_frame_test.cpp)
  target_link_libraries(apsis-drift-craft-frame-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-craft-frame-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME craft-frame-contract COMMAND apsis-drift-craft-frame-tests)
  add_executable(apsis-drift-wayfarer-frame-tests test/wayfarer_frame_test.cpp)
  target_link_libraries(apsis-drift-wayfarer-frame-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-wayfarer-frame-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>)
  add_test(NAME wayfarer-frame-contract COMMAND apsis-drift-wayfarer-frame-tests)
  add_executable(apsis-drift-origin-docking-tests test/origin_docking_test.cpp)
  target_link_libraries(apsis-drift-origin-docking-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-origin-docking-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>)
  add_test(NAME origin-docking-contract COMMAND apsis-drift-origin-docking-tests)
  add_executable(apsis-drift-rigid-body-tests test/rigid_body_test.cpp)
  target_link_libraries(apsis-drift-rigid-body-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-rigid-body-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME rigid-body-contract COMMAND apsis-drift-rigid-body-tests)
  add_executable(apsis-drift-rigid-frame-handoff-tests test/rigid_frame_handoff_test.cpp)
  target_link_libraries(apsis-drift-rigid-frame-handoff-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-rigid-frame-handoff-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME rigid-frame-handoff-contract COMMAND apsis-drift-rigid-frame-handoff-tests)
  set_tests_properties(rigid-frame-handoff-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-physical-rigid-tests test/physical_rigid_test.cpp)
  target_link_libraries(apsis-drift-physical-rigid-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-physical-rigid-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME physical-rigid-contract COMMAND apsis-drift-physical-rigid-tests)
  set_tests_properties(physical-rigid-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-planet-relative-tests test/planet_relative_test.cpp)
  target_link_libraries(apsis-drift-planet-relative-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-planet-relative-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME planet-relative-contract COMMAND apsis-drift-planet-relative-tests)
  set_tests_properties(planet-relative-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-planet-rotation-tests test/planet_rotation_test.cpp)
  target_link_libraries(apsis-drift-planet-rotation-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-planet-rotation-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME planet-rotation-contract COMMAND apsis-drift-planet-rotation-tests)
  set_tests_properties(planet-rotation-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-physical-local-system-tests test/physical_local_system_test.cpp)
  target_link_libraries(apsis-drift-physical-local-system-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-physical-local-system-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME physical-local-system-contract COMMAND apsis-drift-physical-local-system-tests)
  set_tests_properties(physical-local-system-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-body-ephemeris-tests test/body_ephemeris_test.cpp)
  target_link_libraries(apsis-drift-body-ephemeris-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-body-ephemeris-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME body-ephemeris-contract COMMAND apsis-drift-body-ephemeris-tests)
  set_tests_properties(body-ephemeris-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-native-profile-tests test/native_profile_test.cpp)
  target_link_libraries(apsis-drift-native-profile-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-native-profile-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME native-profile-contract COMMAND apsis-drift-native-profile-tests)
  set_tests_properties(native-profile-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-planet-ambient-tests test/planet_ambient_test.cpp)
  target_link_libraries(apsis-drift-planet-ambient-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-planet-ambient-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME planet-ambient-contract COMMAND apsis-drift-planet-ambient-tests)
  set_tests_properties(planet-ambient-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-planet-optics-tests test/planet_optics_test.cpp)
  target_link_libraries(apsis-drift-planet-optics-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-planet-optics-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME planet-optics-contract COMMAND apsis-drift-planet-optics-tests)
  set_tests_properties(planet-optics-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-planet-surface-appearance-tests
    test/planet_surface_appearance_test.cpp)
  target_link_libraries(apsis-drift-planet-surface-appearance-tests
    PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-planet-surface-appearance-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME planet-surface-appearance-contract
    COMMAND apsis-drift-planet-surface-appearance-tests)
  set_tests_properties(planet-surface-appearance-contract PROPERTIES TIMEOUT 30)
  add_executable(apsis-drift-craft-environment-tests test/craft_environment_assessment_test.cpp)
  target_link_libraries(apsis-drift-craft-environment-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-craft-environment-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME craft-environment-contract COMMAND apsis-drift-craft-environment-tests)
  add_executable(apsis-drift-freedom-resource-tests test/freedom_resources_test.cpp)
  target_link_libraries(apsis-drift-freedom-resource-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-freedom-resource-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME freedom-resource-contract COMMAND apsis-drift-freedom-resource-tests)
  add_executable(apsis-drift-freedom-starting-chart-tests test/freedom_starting_chart_test.cpp)
  target_link_libraries(apsis-drift-freedom-starting-chart-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-freedom-starting-chart-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME freedom-starting-chart-contract COMMAND apsis-drift-freedom-starting-chart-tests)
  set_tests_properties(freedom-starting-chart-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-freedom-system-network-tests test/freedom_system_network_test.cpp)
  target_link_libraries(apsis-drift-freedom-system-network-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-freedom-system-network-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME freedom-system-network-contract COMMAND apsis-drift-freedom-system-network-tests)
  set_tests_properties(freedom-system-network-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-freedom-star-search-tests test/freedom_star_search_test.cpp)
  target_link_libraries(apsis-drift-freedom-star-search-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-freedom-star-search-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME freedom-star-search-contract COMMAND apsis-drift-freedom-star-search-tests)
  set_tests_properties(freedom-star-search-contract PROPERTIES TIMEOUT 90)
  foreach(derelict_test IN ITEMS freedom_derelict freedom_derelict_save native_derelict)
    string(REPLACE "_" "-" derelict_name "${derelict_test}")
    add_executable(apsis-drift-${derelict_name}-tests test/${derelict_test}_test.cpp)
    target_link_libraries(apsis-drift-${derelict_name}-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
    target_compile_options(apsis-drift-${derelict_name}-tests PRIVATE
      $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    add_test(NAME ${derelict_name}-contract COMMAND apsis-drift-${derelict_name}-tests)
    set_tests_properties(${derelict_name}-contract PROPERTIES TIMEOUT 90)
  endforeach()
  add_executable(apsis-drift-freedom-knowledge-tests test/freedom_knowledge_test.cpp)
  target_link_libraries(apsis-drift-freedom-knowledge-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-freedom-knowledge-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME freedom-knowledge-contract COMMAND apsis-drift-freedom-knowledge-tests)
  set_tests_properties(freedom-knowledge-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-native-orbital-survey-tests test/native_orbital_survey_test.cpp test/native_survey_commission_scenario.cpp test/native_specimen_sale_scenario.cpp test/native_charted_arrivals_scenario.cpp)
  target_link_libraries(apsis-drift-native-orbital-survey-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_include_directories(apsis-drift-native-orbital-survey-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-native-orbital-survey-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME native-orbital-survey-contract COMMAND apsis-drift-native-orbital-survey-tests)
  set_tests_properties(native-orbital-survey-contract PROPERTIES TIMEOUT 90)
  foreach(commission_test IN ITEMS survey_commissions freedom_survey_commission_save paid_operations paid_operations_save paid_specimen_income specimen_sales_save)
    string(REPLACE "_" "-" commission_name "${commission_test}")
    add_executable(apsis-drift-${commission_name}-tests test/${commission_test}_test.cpp)
    target_link_libraries(apsis-drift-${commission_name}-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
    target_compile_options(apsis-drift-${commission_name}-tests PRIVATE
      $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
    add_test(NAME ${commission_name}-contract COMMAND apsis-drift-${commission_name}-tests)
    set_tests_properties(${commission_name}-contract PROPERTIES TIMEOUT 90)
  endforeach()
  add_executable(apsis-drift-freedom-orbital-survey-save-tests test/freedom_orbital_survey_save_test.cpp)
  target_link_libraries(apsis-drift-freedom-orbital-survey-save-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-freedom-orbital-survey-save-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME freedom-orbital-survey-save-contract COMMAND apsis-drift-freedom-orbital-survey-save-tests)
  set_tests_properties(freedom-orbital-survey-save-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-freedom-local-observation-tests test/freedom_local_observations_test.cpp)
  target_link_libraries(apsis-drift-freedom-local-observation-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-freedom-local-observation-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME freedom-local-observations-contract COMMAND apsis-drift-freedom-local-observation-tests)
  set_tests_properties(freedom-local-observations-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-freedom-jump-targeting-tests test/freedom_jump_targeting_test.cpp)
  target_link_libraries(apsis-drift-freedom-jump-targeting-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-freedom-jump-targeting-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME freedom-jump-targeting-contract COMMAND apsis-drift-freedom-jump-targeting-tests)
  set_tests_properties(freedom-jump-targeting-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-native-first-jump-tests test/native_first_jump_test.cpp)
  target_link_libraries(apsis-drift-native-first-jump-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-native-first-jump-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME native-first-jump-contract COMMAND apsis-drift-native-first-jump-tests)
  set_tests_properties(native-first-jump-contract PROPERTIES TIMEOUT 90)
  add_test(NAME native-uncharted-system-contract COMMAND apsis-drift-native-first-jump-tests --uncharted)
  set_tests_properties(native-uncharted-system-contract PROPERTIES TIMEOUT 90)
  add_test(NAME freedom-observed-surface-lifecycle COMMAND apsis-drift-landed-craft-tests --observations)
  set_tests_properties(freedom-observed-surface-lifecycle PROPERTIES TIMEOUT 90)
  add_test(NAME freedom-reserve-surface-escape COMMAND apsis-drift-landed-craft-tests --resources)
  set_tests_properties(freedom-reserve-surface-escape PROPERTIES TIMEOUT 90)
  set_tests_properties(freedom-resource-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-flight-endurance-research-tests test/flight_endurance_research_test.cpp)
  target_link_libraries(apsis-drift-flight-endurance-research-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-flight-endurance-research-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME flight-endurance-research-contract COMMAND apsis-drift-flight-endurance-research-tests)
  set_tests_properties(flight-endurance-research-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-vacuum-dynamics-tests test/vacuum_dynamics_test.cpp)
  target_link_libraries(apsis-drift-vacuum-dynamics-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-vacuum-dynamics-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME vacuum-dynamics-contract COMMAND apsis-drift-vacuum-dynamics-tests)
  set_tests_properties(vacuum-dynamics-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-coasting-vacuum-tests test/coasting_vacuum_test.cpp)
  target_link_libraries(apsis-drift-coasting-vacuum-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-coasting-vacuum-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME coasting-vacuum-contract COMMAND apsis-drift-coasting-vacuum-tests)
  set_tests_properties(coasting-vacuum-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-central-body-tests test/central_body_dynamics_test.cpp)
  target_link_libraries(apsis-drift-central-body-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-central-body-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME central-body-dynamics-contract COMMAND apsis-drift-central-body-tests)
  set_tests_properties(central-body-dynamics-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-orbital-telemetry-tests test/orbital_telemetry_test.cpp)
  target_link_libraries(apsis-drift-orbital-telemetry-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-orbital-telemetry-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME orbital-telemetry-contract COMMAND apsis-drift-orbital-telemetry-tests)
  set_tests_properties(orbital-telemetry-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-orbit-hold-tests test/orbit_hold_test.cpp)
  target_link_libraries(apsis-drift-orbit-hold-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-orbit-hold-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME orbit-hold-contract COMMAND apsis-drift-orbit-hold-tests)
  set_tests_properties(orbit-hold-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-atmospheric-flight-tests test/atmospheric_flight_test.cpp)
  target_link_libraries(apsis-drift-atmospheric-flight-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-atmospheric-flight-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
  add_test(NAME atmospheric-flight-contract COMMAND apsis-drift-atmospheric-flight-tests)
  set_tests_properties(atmospheric-flight-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-landed-craft-tests test/landed_craft_test.cpp)
  target_include_directories(apsis-drift-landed-craft-tests PRIVATE src/godot)
  target_link_libraries(apsis-drift-landed-craft-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-landed-craft-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME landed-craft-contract COMMAND apsis-drift-landed-craft-tests)
  add_test(NAME physical-daynight-surface-contract COMMAND apsis-drift-landed-craft-tests --daynight)
  set_tests_properties(physical-daynight-surface-contract PROPERTIES TIMEOUT 90)
  add_test(NAME selected-terrain-surface-lifecycle COMMAND apsis-drift-landed-craft-tests --local-detail)
  set_tests_properties(selected-terrain-surface-lifecycle PROPERTIES TIMEOUT 180)
  add_test(NAME planet-surface-walk-contract COMMAND apsis-drift-landed-craft-tests --walk)
  set_tests_properties(landed-craft-contract PROPERTIES TIMEOUT 90)
  foreach(impact_contract IN ITEMS terrain-impact craft-condition freedom-condition-save native-impact)
    string(REPLACE "-" "_" impact_source "${impact_contract}")
    add_executable(apsis-drift-${impact_contract}-tests test/${impact_source}_test.cpp)
    target_include_directories(apsis-drift-${impact_contract}-tests PRIVATE src src/godot)
    target_link_libraries(apsis-drift-${impact_contract}-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
    target_compile_options(apsis-drift-${impact_contract}-tests PRIVATE
      $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
    add_test(NAME ${impact_contract}-contract COMMAND apsis-drift-${impact_contract}-tests)
    set_tests_properties(${impact_contract}-contract PROPERTIES TIMEOUT 180)
  endforeach()
  add_executable(apsis-drift-terrain-touchdown-tests test/terrain_touchdown_test.cpp)
  target_include_directories(apsis-drift-terrain-touchdown-tests PRIVATE src src/godot)
  target_link_libraries(apsis-drift-terrain-touchdown-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-terrain-touchdown-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME terrain-touchdown-contract COMMAND apsis-drift-terrain-touchdown-tests)
  set_tests_properties(terrain-touchdown-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-touchdown-tests test/touchdown_test.cpp)
  target_link_libraries(apsis-drift-touchdown-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-touchdown-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME touchdown-envelope-contract COMMAND apsis-drift-touchdown-tests)
  set_tests_properties(touchdown-envelope-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-rotation-coast-tests
    test/godot/rotation_coast_test.cpp)
  target_link_libraries(apsis-drift-rotation-coast-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-rotation-coast-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME godot-rotation-coast-contract COMMAND apsis-drift-rotation-coast-tests)
  set_tests_properties(godot-rotation-coast-contract PROPERTIES TIMEOUT 90)
endif ()
