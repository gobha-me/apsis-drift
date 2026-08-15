if (NOT DEFINED APSIS_DRIFT_BIN)
  message(FATAL_ERROR "APSIS_DRIFT_BIN is required")
endif ()
if (NOT DEFINED REPORT_DIR)
  message(FATAL_ERROR "REPORT_DIR is required")
endif ()

find_program(SCRIPT_BIN script REQUIRED)

function(check_acceptance driver profile expected_framebuffer)
  set(report "${REPORT_DIR}/flight-deck-${driver}.json")
  execute_process(
    COMMAND "${SCRIPT_BIN}" --quiet --return --command
            "${APSIS_DRIFT_BIN} --flight-deck-acceptance --driver ${driver} --profile ${profile} --report ${report}"
            /dev/null
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_VARIABLE error
  )
  if (NOT result EQUAL 0)
    message(FATAL_ERROR
      "${driver} acceptance failed (${result})\nstderr:\n${error}")
  endif ()

  file(READ "${report}" json)
  foreach(field schema_version scenario seed simulation_hz final_tick
                command_count flight_checksum framebuffer_checksum
                render_profile presentation)
    string(JSON value ERROR_VARIABLE json_error GET "${json}" "${field}")
    if (json_error)
      message(FATAL_ERROR
        "${driver} report field '${field}' failed to parse: ${json_error}")
    endif ()
    set("${field}" "${value}")
  endforeach ()

  if (NOT schema_version STREQUAL "1" OR
      NOT scenario STREQUAL "v0.2-flight-deck" OR
      NOT seed STREQUAL "12648430" OR
      NOT simulation_hz STREQUAL "120" OR
      NOT final_tick STREQUAL "240" OR
      NOT command_count STREQUAL "18" OR
      NOT flight_checksum STREQUAL "15302063256845754841" OR
      NOT framebuffer_checksum STREQUAL "${expected_framebuffer}" OR
      NOT render_profile STREQUAL "${profile}" OR
      NOT presentation STREQUAL "${driver}")
    message(FATAL_ERROR
      "${driver} acceptance report does not match the canonical scenario:\n${json}")
  endif ()

  set("${driver}_flight_checksum" "${flight_checksum}" PARENT_SCOPE)
endfunction()

check_acceptance(ansi remote 4248103746500193130)
check_acceptance(kitty local 14472657128233142808)

if (NOT ansi_flight_checksum STREQUAL kitty_flight_checksum)
  message(FATAL_ERROR
    "presentation path changed deterministic flight state: "
    "ANSI=${ansi_flight_checksum}, Kitty=${kitty_flight_checksum}")
endif ()
