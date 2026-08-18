if (NOT DEFINED APSIS_DRIFT_BIN)
  message(FATAL_ERROR "APSIS_DRIFT_BIN is required")
endif ()
if (NOT DEFINED REPORT_DIR)
  message(FATAL_ERROR "REPORT_DIR is required")
endif ()

function(check_jump_acceptance driver)
  set(report "${REPORT_DIR}/intersystem-jump-${driver}.json")
  execute_process(
    COMMAND "${APSIS_DRIFT_BIN}" --intersystem-jump-acceptance
            --driver "${driver}" --profile remote --report "${report}"
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_VARIABLE error
  )
  if (NOT result EQUAL 0)
    message(FATAL_ERROR
      "${driver} jump acceptance failed (${result})\nstderr:\n${error}")
  endif ()

  file(READ "${report}" json)
  foreach(field schema_version scenario presentation destination_system_id
                reference_planet_id committed_tick arrival_tick
                arrival_checksum framebuffer_checksum)
    string(JSON value ERROR_VARIABLE json_error GET "${json}" "${field}")
    if (json_error)
      message(FATAL_ERROR
        "${driver} report field '${field}' failed to parse: ${json_error}")
    endif ()
    set("${field}" "${value}")
  endforeach ()
  if (NOT schema_version STREQUAL "1" OR
      NOT scenario STREQUAL "v0.4.8-assisted-intersystem-jump" OR
      NOT presentation STREQUAL "${driver}" OR
      NOT destination_system_id STREQUAL "system-28630482e6b15573" OR
      NOT reference_planet_id STREQUAL "planet-a1dc72d8fd111fbb" OR
      NOT committed_tick STREQUAL "360" OR
      NOT arrival_tick STREQUAL "600" OR
      NOT arrival_checksum STREQUAL "5687260627167661077" OR
      NOT framebuffer_checksum STREQUAL "4656956508158175312")
    message(FATAL_ERROR
      "${driver} jump acceptance report is not canonical:\n${json}")
  endif ()

  set("${driver}_arrival_checksum" "${arrival_checksum}" PARENT_SCOPE)
  set("${driver}_framebuffer_checksum" "${framebuffer_checksum}" PARENT_SCOPE)
endfunction ()

check_jump_acceptance(ansi)
check_jump_acceptance(kitty)

if (NOT ansi_arrival_checksum STREQUAL kitty_arrival_checksum OR
    NOT ansi_framebuffer_checksum STREQUAL kitty_framebuffer_checksum)
  message(FATAL_ERROR
    "presentation path changed authoritative arrival or transit pixels")
endif ()
