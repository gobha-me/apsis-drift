if (NOT DEFINED APSIS_DRIFT_BIN)
  message(FATAL_ERROR "APSIS_DRIFT_BIN is required")
endif ()
if (NOT DEFINED REPORT_DIR)
  message(FATAL_ERROR "REPORT_DIR is required")
endif ()

function(check_intersystem_planetfall driver)
  set(report "${REPORT_DIR}/intersystem-planetfall-${driver}.json")
  set(snapshot "${REPORT_DIR}/intersystem-planetfall-${driver}.ppm")
  execute_process(
    COMMAND "${APSIS_DRIFT_BIN}" --intersystem-planetfall-acceptance
            --driver "${driver}" --profile remote --report "${report}"
            --snapshot "${snapshot}"
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_VARIABLE error
  )
  if (NOT result EQUAL 0)
    message(FATAL_ERROR
      "${driver} intersystem Planetfall acceptance failed (${result})\nstderr:\n${error}")
  endif ()
  file(READ "${report}" json)
  foreach(field schema_version scenario presentation planet_id target_id
                abort_orbit_tick abort_orbit_checksum completion_tick
                completed_flight_checksum world_delta_count framebuffer_checksum)
    string(JSON value ERROR_VARIABLE json_error GET "${json}" "${field}")
    if (json_error)
      message(FATAL_ERROR
        "${driver} report field '${field}' failed to parse: ${json_error}")
    endif ()
    set("${field}" "${value}")
  endforeach ()
  string(JSON entry_count LENGTH "${json}" entries)
  foreach(index RANGE 0 2)
    string(JSON entry_name_${index} GET "${json}" entries ${index} name)
    string(JSON entry_tick_${index} GET "${json}" entries ${index} terrain_tick)
    string(JSON entry_checksum_${index} GET "${json}" entries ${index} flight_checksum)
  endforeach ()
  if (NOT schema_version STREQUAL "1" OR
      NOT scenario STREQUAL "v0.4.11-entry-anywhere-planetfall" OR
      NOT presentation STREQUAL "${driver}" OR
      NOT planet_id STREQUAL "planet-a1dc72d8fd111fbb" OR
      NOT target_id STREQUAL "signal-9936ac67f2245d20" OR
      NOT entry_count STREQUAL "3" OR
      NOT entry_name_0 STREQUAL "correct-side" OR
      NOT entry_tick_0 STREQUAL "13526" OR
      NOT entry_checksum_0 STREQUAL "17705752855609448626" OR
      NOT entry_name_1 STREQUAL "early" OR
      NOT entry_tick_1 STREQUAL "13705" OR
      NOT entry_checksum_1 STREQUAL "16196634877403872426" OR
      NOT entry_name_2 STREQUAL "opposite-side" OR
      NOT entry_tick_2 STREQUAL "13934" OR
      NOT entry_checksum_2 STREQUAL "459017227781827419" OR
      NOT abort_orbit_tick STREQUAL "3577" OR
      NOT abort_orbit_checksum STREQUAL "7537708600294715479" OR
      NOT completion_tick STREQUAL "1020" OR
      NOT completed_flight_checksum STREQUAL "5463503741755767666" OR
      NOT world_delta_count STREQUAL "1" OR
      NOT framebuffer_checksum STREQUAL "15634582835738947125")
    message(FATAL_ERROR
      "${driver} intersystem Planetfall report is not canonical:\n${json}")
  endif ()
  file(SHA256 "${snapshot}" snapshot_sha)
  set("${driver}_abort_tick" "${abort_orbit_tick}" PARENT_SCOPE)
  set("${driver}_abort" "${abort_orbit_checksum}" PARENT_SCOPE)
  set("${driver}_completion" "${completion_tick}" PARENT_SCOPE)
  set("${driver}_flight" "${completed_flight_checksum}" PARENT_SCOPE)
  set("${driver}_frame" "${framebuffer_checksum}" PARENT_SCOPE)
  set("${driver}_snapshot" "${snapshot_sha}" PARENT_SCOPE)
  set("${driver}_json" "${json}" PARENT_SCOPE)
endfunction ()

check_intersystem_planetfall(ansi)
check_intersystem_planetfall(kitty)
if (NOT ansi_abort_tick STREQUAL kitty_abort_tick OR
    NOT ansi_abort STREQUAL kitty_abort OR
    NOT ansi_completion STREQUAL kitty_completion OR
    NOT ansi_flight STREQUAL kitty_flight OR
    NOT ansi_frame STREQUAL kitty_frame OR
    NOT ansi_snapshot STREQUAL kitty_snapshot)
  message(FATAL_ERROR
    "Kitty and ANSI intersystem Planetfall acceptance results diverged")
endif ()
