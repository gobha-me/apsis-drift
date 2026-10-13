if (APSIS_DRIFT_TESTS AND BUILD_TESTING)
  add_executable(apsis-drift-asset-manifest-tests
    test/asset_manifest_test.cpp)
  target_link_libraries(apsis-drift-asset-manifest-tests
    PRIVATE apsis_drift_asset_manifest nlohmann_json::nlohmann_json)
  add_test(NAME asset-manifest-contract
    COMMAND apsis-drift-asset-manifest-tests)
  add_test(NAME asset-provenance-manifest
    COMMAND apsis-drift-asset-validator
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/provenance.json")
endif ()
