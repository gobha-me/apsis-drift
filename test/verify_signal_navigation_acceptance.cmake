if (NOT DEFINED APSIS_DRIFT_BIN)
  message(FATAL_ERROR "APSIS_DRIFT_BIN is required")
endif ()
if (NOT DEFINED REPORT_DIR)
  message(FATAL_ERROR "REPORT_DIR is required")
endif ()

find_program(SCRIPT_BIN script REQUIRED)

function(check_signal_acceptance driver profile)
  set(report "${REPORT_DIR}/signal-navigation-${driver}.json")
  execute_process(
    COMMAND "${SCRIPT_BIN}" --quiet --return --command
            "${APSIS_DRIFT_BIN} --signal-navigation-acceptance --driver ${driver} --profile ${profile} --report ${report}"
            /dev/null
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_VARIABLE error
  )
  if (NOT result EQUAL 0)
    message(FATAL_ERROR
      "${driver} signal acceptance failed (${result})\nstderr:\n${error}")
  endif ()

  file(READ "${report}" json)
  foreach(field schema_version scenario seed simulation_hz target_ordinal
                target_id selection_tick reached_tick command_count
                final_status final_distance_metres flight_checksum
                framebuffer_checksum render_profile presentation)
    string(JSON value ERROR_VARIABLE json_error GET "${json}" "${field}")
    if (json_error)
      message(FATAL_ERROR
        "${driver} report field '${field}' failed to parse: ${json_error}")
    endif ()
    set("${field}" "${value}")
  endforeach ()

  if (NOT schema_version STREQUAL "1" OR
      NOT scenario STREQUAL "v0.4-signal-navigation" OR
      NOT seed STREQUAL "42" OR
      NOT simulation_hz STREQUAL "120" OR
      NOT target_ordinal STREQUAL "0" OR
      NOT target_id STREQUAL "signal-945eaa623b2b8497" OR
      NOT selection_tick STREQUAL "0" OR
      NOT reached_tick STREQUAL "1072" OR
      NOT command_count STREQUAL "1" OR
      NOT final_status STREQUAL "reached" OR
      final_distance_metres GREATER 1000.0 OR
      NOT flight_checksum STREQUAL "9743322914782455886" OR
      NOT framebuffer_checksum MATCHES "^[0-9]+$" OR
      framebuffer_checksum STREQUAL "0" OR
      NOT render_profile STREQUAL "${profile}" OR
      NOT presentation STREQUAL "${driver}")
    message(FATAL_ERROR
      "${driver} signal acceptance report is not canonical:\n${json}")
  endif ()

  set("${driver}_flight_checksum" "${flight_checksum}" PARENT_SCOPE)
  set("${driver}_reached_tick" "${reached_tick}" PARENT_SCOPE)
endfunction ()

check_signal_acceptance(ansi remote)
check_signal_acceptance(kitty local)

if (NOT ansi_flight_checksum STREQUAL kitty_flight_checksum OR
    NOT ansi_reached_tick STREQUAL kitty_reached_tick)
  message(FATAL_ERROR
    "presentation path changed deterministic navigation state")
endif ()
