if (NOT DEFINED APSIS_DRIFT_BIN)
  message(FATAL_ERROR "APSIS_DRIFT_BIN is required")
endif ()
if (NOT DEFINED REPORT_DIR)
  message(FATAL_ERROR "REPORT_DIR is required")
endif ()

function(run_signal_run driver profile suffix output_variable)
  set(report "${REPORT_DIR}/signal-run-${driver}-${suffix}.json")
  execute_process(
    COMMAND "${APSIS_DRIFT_BIN}" --signal-run-acceptance
            --driver "${driver}" --profile "${profile}" --report "${report}"
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_VARIABLE error
  )
  if (NOT result EQUAL 0)
    message(FATAL_ERROR
      "${driver} Signal Run failed (${result})\nstderr:\n${error}")
  endif ()
  file(READ "${report}" json)
  set("${output_variable}" "${json}" PARENT_SCOPE)
endfunction ()

function(check_signal_run driver profile)
  run_signal_run("${driver}" "${profile}" first first_json)
  run_signal_run("${driver}" "${profile}" second second_json)
  if (NOT first_json STREQUAL second_json)
    message(FATAL_ERROR
      "${driver} Signal Run did not reproduce exactly")
  endif ()

  foreach(field schema_version scenario seed station_id target_id launch_tick
                atmospheric_tick terrain_tick reached_tick completion_tick
                orbital_return_tick checkpoint_flight_checksum
                resumed_flight_checksum framebuffer_checksum discovery_count
                world_delta_count final_location final_objective
                render_profile presentation viewport_width viewport_height)
    string(JSON value ERROR_VARIABLE json_error GET "${first_json}" "${field}")
    if (json_error)
      message(FATAL_ERROR
        "${driver} Signal Run field '${field}' failed to parse: ${json_error}")
    endif ()
    set("${field}" "${value}")
  endforeach ()

  if (NOT schema_version STREQUAL "1" OR
      NOT scenario STREQUAL "v0.4-signal-run" OR
      NOT seed STREQUAL "42" OR
      NOT station_id STREQUAL "station-ce51e866ec4e032d" OR
      NOT target_id STREQUAL "signal-71d4c959dcd64423" OR
      NOT launch_tick STREQUAL "0" OR
      NOT atmospheric_tick STREQUAL "12168" OR
      NOT terrain_tick STREQUAL "23675" OR
      NOT reached_tick STREQUAL "24303" OR
      NOT completion_tick STREQUAL "24722" OR
      NOT orbital_return_tick STREQUAL "53171" OR
      NOT checkpoint_flight_checksum STREQUAL "9296960089338770158" OR
      NOT resumed_flight_checksum STREQUAL checkpoint_flight_checksum OR
      NOT framebuffer_checksum MATCHES "^[0-9]+$" OR
      framebuffer_checksum STREQUAL "0" OR
      NOT discovery_count STREQUAL "1" OR
      NOT world_delta_count STREQUAL "1" OR
      NOT final_location STREQUAL "docked_at_origin" OR
      NOT final_objective STREQUAL "completed" OR
      NOT render_profile STREQUAL "${profile}" OR
      NOT presentation STREQUAL "${driver}")
    message(FATAL_ERROR
      "${driver} Signal Run report is not canonical:\n${first_json}")
  endif ()

  set("${driver}_flight_checksum" "${checkpoint_flight_checksum}" PARENT_SCOPE)
  set("${driver}_completion_tick" "${completion_tick}" PARENT_SCOPE)
  set("${driver}_return_tick" "${orbital_return_tick}" PARENT_SCOPE)
endfunction ()

check_signal_run(ansi remote)
check_signal_run(kitty local)

if (NOT ansi_flight_checksum STREQUAL kitty_flight_checksum OR
    NOT ansi_completion_tick STREQUAL kitty_completion_tick OR
    NOT ansi_return_tick STREQUAL kitty_return_tick)
  message(FATAL_ERROR
    "presentation path changed deterministic Signal Run state")
endif ()
