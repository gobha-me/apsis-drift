if (NOT DEFINED APSIS_DRIFT_BIN)
  message(FATAL_ERROR "APSIS_DRIFT_BIN is required")
endif ()
if (NOT DEFINED REPORT_DIR)
  message(FATAL_ERROR "REPORT_DIR is required")
endif ()

function(check_headless_driver requested expected)
  set(report "${REPORT_DIR}/headless-driver-${requested}.json")
  execute_process(
    COMMAND "${APSIS_DRIFT_BIN}" --benchmark 2 --driver "${requested}"
            --profile remote --seed 12648430 --report "${report}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
  )
  if (NOT result EQUAL 0)
    message(FATAL_ERROR
      "${requested} benchmark failed (${result})\nstdout:\n${output}\nstderr:\n${error}")
  endif ()

  string(FIND "${output}" "display: ${expected} (headless" display_position)
  if (display_position LESS 0)
    message(FATAL_ERROR
      "${requested} benchmark did not report active ${expected} driver:\n${output}")
  endif ()

  file(READ "${report}" json)
  foreach(field presentation frames total_bytes checksum)
    string(JSON value ERROR_VARIABLE json_error GET "${json}" "${field}")
    if (json_error)
      message(FATAL_ERROR
        "${requested} report field '${field}' failed to parse: ${json_error}")
    endif ()
    set("${field}" "${value}")
  endforeach ()
  if (NOT presentation STREQUAL "${expected}" OR
      NOT frames STREQUAL "2" OR
      total_bytes LESS 1 OR
      NOT checksum MATCHES "^[0-9]+$" OR
      checksum STREQUAL "0")
    message(FATAL_ERROR
      "${requested} benchmark report is not truthful:\n${json}")
  endif ()

  set("${requested}_total_bytes" "${total_bytes}" PARENT_SCOPE)
  set("${requested}_checksum" "${checksum}" PARENT_SCOPE)
endfunction ()

check_headless_driver(automatic kitty)
check_headless_driver(kitty kitty)
check_headless_driver(ansi ansi)
check_headless_driver(fallback fallback)

if (ansi_total_bytes STREQUAL kitty_total_bytes OR
    fallback_total_bytes STREQUAL kitty_total_bytes OR
    fallback_total_bytes STREQUAL ansi_total_bytes)
  message(FATAL_ERROR
    "headless drivers did not produce distinct encoded-byte signatures: "
    "Kitty=${kitty_total_bytes}, ANSI=${ansi_total_bytes}, "
    "fallback=${fallback_total_bytes}")
endif ()
if (NOT automatic_checksum STREQUAL kitty_checksum OR
    NOT ansi_checksum STREQUAL kitty_checksum OR
    NOT fallback_checksum STREQUAL kitty_checksum)
  message(FATAL_ERROR
    "headless driver changed the application framebuffer checksum")
endif ()
