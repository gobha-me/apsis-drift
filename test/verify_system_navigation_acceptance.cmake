if (NOT DEFINED APSIS_DRIFT_BIN)
  message(FATAL_ERROR "APSIS_DRIFT_BIN is required")
endif ()
if (NOT DEFINED REPORT_DIR)
  message(FATAL_ERROR "REPORT_DIR is required")
endif ()

find_program(SCRIPT_BIN script REQUIRED)

function(check_system_acceptance driver)
  set(report "${REPORT_DIR}/system-navigation-${driver}.json")
  execute_process(
    COMMAND "${SCRIPT_BIN}" --quiet --return --command
            "${APSIS_DRIFT_BIN} --system-navigation-acceptance --driver ${driver} --profile remote --report ${report}"
            /dev/null
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_VARIABLE error
  )
  if (NOT result EQUAL 0)
    message(FATAL_ERROR
      "${driver} system acceptance failed (${result})\nstderr:\n${error}")
  endif ()

  file(READ "${report}" json)
  foreach(field schema_version scenario presentation system_id star_id
                target_planet_id target_name visible_planets selected_visible)
    string(JSON value ERROR_VARIABLE json_error GET "${json}" "${field}")
    if (json_error)
      message(FATAL_ERROR
        "${driver} report field '${field}' failed to parse: ${json_error}")
    endif ()
    set("${field}" "${value}")
  endforeach ()
  string(JSON frames GET "${json}" benchmark frames)
  string(JSON benchmark_schema_version GET "${json}" benchmark
         schema_version)
  string(JSON checksum GET "${json}" benchmark checksum)
  string(JSON checksum_type TYPE "${json}" benchmark checksum)
  string(JSON workload GET "${json}" benchmark workload)
  string(JSON benchmark_presentation GET "${json}" benchmark presentation)
  string(JSON total_bytes GET "${json}" benchmark total_bytes)
  string(JSON total_bytes_type TYPE "${json}" benchmark total_bytes)
  if (NOT schema_version STREQUAL "2" OR
      NOT benchmark_schema_version STREQUAL "1" OR
      NOT scenario STREQUAL "v0.4.6-local-system-navigation" OR
      NOT presentation STREQUAL "${driver}" OR
      NOT benchmark_presentation STREQUAL "${driver}" OR
      NOT system_id STREQUAL "2910174744474113395" OR
      NOT star_id STREQUAL "4391435423288202480" OR
      NOT target_planet_id STREQUAL "11663323411267002299" OR
      NOT frames STREQUAL "6" OR
      NOT workload STREQUAL "local-system-320x240-rgba" OR
      visible_planets LESS 1 OR
      NOT selected_visible OR
      NOT total_bytes_type STREQUAL "STRING" OR
      NOT total_bytes MATCHES "^[1-9][0-9]*$" OR
      NOT checksum_type STREQUAL "STRING" OR
      NOT checksum MATCHES "^[0-9]+$" OR
      checksum STREQUAL "0")
    message(FATAL_ERROR
      "${driver} system acceptance report is not canonical:\n${json}")
  endif ()

  set("${driver}_system_id" "${system_id}" PARENT_SCOPE)
  set("${driver}_target_id" "${target_planet_id}" PARENT_SCOPE)
  set("${driver}_checksum" "${checksum}" PARENT_SCOPE)
  set("${driver}_total_bytes" "${total_bytes}" PARENT_SCOPE)
endfunction ()

check_system_acceptance(ansi)
check_system_acceptance(kitty)

if (NOT ansi_system_id STREQUAL kitty_system_id OR
    NOT ansi_target_id STREQUAL kitty_target_id OR
    NOT ansi_checksum STREQUAL kitty_checksum)
  message(FATAL_ERROR
    "presentation path changed authoritative system identity or pixels")
endif ()
if (ansi_total_bytes STREQUAL kitty_total_bytes)
  message(FATAL_ERROR
    "ANSI and Kitty system acceptance used the same encoded-byte signature")
endif ()
