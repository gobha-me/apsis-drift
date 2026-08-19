if (NOT DEFINED APSIS_DRIFT_BIN)
  message(FATAL_ERROR "APSIS_DRIFT_BIN is required")
endif ()
if (NOT DEFINED REPORT_DIR)
  message(FATAL_ERROR "REPORT_DIR is required")
endif ()

function(check_system_flight)
  set(report "${REPORT_DIR}/system-flight-application-framebuffer.json")
  set(snapshot "${REPORT_DIR}/system-flight-application-framebuffer.ppm")
  execute_process(
    COMMAND "${APSIS_DRIFT_BIN}" --system-flight-acceptance
            --profile remote --report "${report}"
            --snapshot "${snapshot}"
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_VARIABLE error
  )
  if (NOT result EQUAL 0)
    message(FATAL_ERROR
      "system-flight acceptance failed (${result})\nstderr:\n${error}")
  endif ()
  file(READ "${report}" json)
  foreach(field schema_version scenario evidence_scope system_id planet_id
                arrival_tick insertion_tick host_steps system_flight_checksum
                orbital_flight_checksum framebuffer_checksum)
    string(JSON value ERROR_VARIABLE json_error GET "${json}" "${field}")
    if (json_error)
      message(FATAL_ERROR
        "report field '${field}' failed to parse: ${json_error}")
    endif ()
    set("${field}" "${value}")
  endforeach ()
  if (NOT schema_version STREQUAL "2" OR
      NOT scenario STREQUAL "v0.4.9-system-flight" OR
      NOT evidence_scope STREQUAL "application_framebuffer" OR
      NOT system_id STREQUAL "system-28630482e6b15573" OR
      NOT planet_id STREQUAL "planet-a1dc72d8fd111fbb" OR
      NOT arrival_tick STREQUAL "600" OR
      NOT insertion_tick STREQUAL "9047" OR
      NOT host_steps STREQUAL "4084" OR
      NOT system_flight_checksum STREQUAL "12996023908337151515" OR
      NOT orbital_flight_checksum STREQUAL "6543655391732023443" OR
      NOT framebuffer_checksum STREQUAL "2134678291051779558")
    message(FATAL_ERROR
      "system-flight report is not canonical:\n${json}")
  endif ()
  file(SHA256 "${snapshot}" snapshot_sha)
endfunction ()

check_system_flight()
