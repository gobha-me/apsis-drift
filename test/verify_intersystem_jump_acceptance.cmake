if (NOT DEFINED APSIS_DRIFT_BIN)
  message(FATAL_ERROR "APSIS_DRIFT_BIN is required")
endif ()
if (NOT DEFINED REPORT_DIR)
  message(FATAL_ERROR "REPORT_DIR is required")
endif ()

function(check_jump_acceptance)
  set(report "${REPORT_DIR}/intersystem-jump-application-framebuffer.json")
  execute_process(
    COMMAND "${APSIS_DRIFT_BIN}" --intersystem-jump-acceptance
            --profile remote --report "${report}"
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_VARIABLE error
  )
  if (NOT result EQUAL 0)
    message(FATAL_ERROR
      "jump acceptance failed (${result})\nstderr:\n${error}")
  endif ()

  file(READ "${report}" json)
  foreach(field schema_version scenario evidence_scope destination_system_id
                reference_planet_id committed_tick arrival_tick
                arrival_checksum assisted_quality
                pilot_initial_heading_error_millidegrees
                pilot_initial_velocity_error_basis_points
                pilot_aligned_checksum pilot_offset_checksum
                pilot_opposed_checksum pilot_offset_distance_metres
                pilot_opposed_distance_metres framebuffer_checksum)
    string(JSON value ERROR_VARIABLE json_error GET "${json}" "${field}")
    if (json_error)
      message(FATAL_ERROR
        "report field '${field}' failed to parse: ${json_error}")
    endif ()
    set("${field}" "${value}")
  endforeach ()
  if (NOT schema_version STREQUAL "3" OR
      NOT scenario STREQUAL "v0.4.15-pilot-ftl-alignment" OR
      NOT evidence_scope STREQUAL "application_framebuffer" OR
      NOT destination_system_id STREQUAL "system-28630482e6b15573" OR
      NOT reference_planet_id STREQUAL "planet-a1dc72d8fd111fbb" OR
      NOT committed_tick STREQUAL "360" OR
      NOT arrival_tick STREQUAL "600" OR
      NOT arrival_checksum STREQUAL "14671588990613181972" OR
      NOT assisted_quality STREQUAL "ALIGNED" OR
      NOT pilot_initial_heading_error_millidegrees STREQUAL "-16160" OR
      NOT pilot_initial_velocity_error_basis_points STREQUAL "-473" OR
      NOT pilot_aligned_checksum STREQUAL "14671588990613181972" OR
      NOT pilot_offset_checksum STREQUAL "4112027265386174051" OR
      NOT pilot_opposed_checksum STREQUAL "4541203662738406157" OR
      NOT pilot_offset_distance_metres STREQUAL "503720000.000" OR
      NOT pilot_opposed_distance_metres STREQUAL "14198903999.135" OR
      NOT framebuffer_checksum STREQUAL "4656956508158175312")
    message(FATAL_ERROR
      "jump acceptance report is not canonical:\n${json}")
  endif ()

endfunction ()

check_jump_acceptance()
