if (NOT DEFINED APSIS_DRIFT_BIN)
  message(FATAL_ERROR "APSIS_DRIFT_BIN is required")
endif ()
if (NOT DEFINED REPORT_DIR)
  message(FATAL_ERROR "REPORT_DIR is required")
endif ()

function(check_intersystem_return)
  set(report "${REPORT_DIR}/intersystem-return-application-framebuffer.json")
  set(snapshot "${REPORT_DIR}/intersystem-return-application-framebuffer.ppm")
  execute_process(
    COMMAND "${APSIS_DRIFT_BIN}" --intersystem-return-acceptance
            --profile remote --report "${report}"
            --snapshot "${snapshot}"
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_VARIABLE error
  )
  if (NOT result EQUAL 0)
    message(FATAL_ERROR
      "intersystem return acceptance failed (${result})\nstderr:\n${error}")
  endif ()
  file(READ "${report}" json)
  foreach(field schema_version scenario evidence_scope origin_station_id
                departure_tick return_commit_tick origin_arrival_tick
                docking_tick departure_checksum origin_arrival_checksum
                docked_return_checksum discovery_count world_delta_count
                framebuffer_checksum)
    string(JSON value ERROR_VARIABLE json_error GET "${json}" "${field}")
    if (json_error)
      message(FATAL_ERROR
        "report field '${field}' failed to parse: ${json_error}")
    endif ()
    set("${field}" "${value}")
  endforeach ()
  if (NOT schema_version STREQUAL "2" OR
      NOT scenario STREQUAL "v0.4.12-intersystem-return" OR
      NOT evidence_scope STREQUAL "application_framebuffer" OR
      NOT origin_station_id STREQUAL "station-ce51e866ec4e032d" OR
      NOT departure_tick STREQUAL "600" OR
      NOT return_commit_tick STREQUAL "1020" OR
      NOT origin_arrival_tick STREQUAL "1260" OR
      NOT docking_tick STREQUAL "5929" OR
      NOT departure_checksum STREQUAL "15121722808198037731" OR
      NOT origin_arrival_checksum STREQUAL "6075957129980954659" OR
      NOT docked_return_checksum STREQUAL "1077216484693038431" OR
      NOT discovery_count STREQUAL "1" OR
      NOT world_delta_count STREQUAL "1" OR
      NOT framebuffer_checksum STREQUAL "7194522011593911474")
    message(FATAL_ERROR
      "intersystem return report is not canonical:\n${json}")
  endif ()
  file(SHA256 "${snapshot}" snapshot_sha)
endfunction ()

check_intersystem_return()
