if (NOT DEFINED APSIS_DRIFT_BIN)
  message(FATAL_ERROR "APSIS_DRIFT_BIN is required")
endif ()
if (NOT DEFINED REPORT_DIR)
  message(FATAL_ERROR "REPORT_DIR is required")
endif ()

function(check_intersystem_contract driver)
  set(report "${REPORT_DIR}/intersystem-contract-${driver}.json")
  set(snapshot "${REPORT_DIR}/intersystem-contract-${driver}.ppm")
  execute_process(
    COMMAND "${APSIS_DRIFT_BIN}" --intersystem-contract-acceptance
            --driver "${driver}" --profile remote --report "${report}"
            --snapshot "${snapshot}"
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_VARIABLE error
  )
  if (NOT result EQUAL 0)
    message(FATAL_ERROR
      "${driver} intersystem contract acceptance failed (${result})\nstderr:\n${error}")
  endif ()
  file(READ "${report}" json)
  foreach(field schema_version scenario presentation seed mission_id
                target_system_id target_planet_id target_objective_id
                origin_station_id final_tick final_mission_phase
                final_authoritative_checksum wrong_side_recovery_checksum
                target_system_planet_count
                target_system_initial_framebuffer_checksum
                target_system_moved_framebuffer_checksum discovery_count
                world_delta_count framebuffer_checksum)
    string(JSON value ERROR_VARIABLE json_error GET "${json}" "${field}")
    if (json_error)
      message(FATAL_ERROR
        "${driver} report field '${field}' failed to parse: ${json_error}")
    endif ()
    set("${field}" "${value}")
  endforeach ()
  string(JSON checkpoint_count LENGTH "${json}" checkpoints)
  if (NOT schema_version STREQUAL "1" OR
      NOT scenario STREQUAL "v0.4.13-intersystem-contract-loop" OR
      NOT presentation STREQUAL "${driver}" OR
      NOT seed STREQUAL "42" OR
      NOT mission_id STREQUAL "mission-d8e068532886e95b" OR
      NOT target_system_id STREQUAL "system-28630482e6b15573" OR
      NOT target_planet_id STREQUAL "planet-a1dc72d8fd111fbb" OR
      NOT target_objective_id STREQUAL "signal-9936ac67f2245d20" OR
      NOT origin_station_id STREQUAL "station-ce51e866ec4e032d" OR
      NOT final_tick STREQUAL "31535" OR
      NOT final_mission_phase STREQUAL "turned_in" OR
      NOT final_authoritative_checksum STREQUAL "9496404445183332939" OR
      NOT wrong_side_recovery_checksum STREQUAL "7537708600294715479" OR
      NOT target_system_planet_count STREQUAL "6" OR
      NOT target_system_initial_framebuffer_checksum STREQUAL
          "13519396001762605819" OR
      NOT target_system_moved_framebuffer_checksum STREQUAL
          "5625436622452186767" OR
      NOT discovery_count STREQUAL "1" OR
      NOT world_delta_count STREQUAL "1" OR
      NOT framebuffer_checksum STREQUAL "15648935810629710496" OR
      NOT checkpoint_count STREQUAL "6")
    message(FATAL_ERROR
      "${driver} intersystem contract report is not canonical:\n${json}")
  endif ()
  set(expected_names docked outbound-transit target-system planet-side
                     origin-return returned-docked)
  set(expected_ticks 0 360 600 9467 30472 31535)
  math(EXPR final_index "${checkpoint_count} - 1")
  foreach(index RANGE 0 ${final_index})
    list(GET expected_names ${index} expected_name)
    list(GET expected_ticks ${index} expected_tick)
    string(JSON name GET "${json}" checkpoints ${index} name)
    string(JSON tick GET "${json}" checkpoints ${index} tick)
    string(JSON resumed GET "${json}" checkpoints ${index}
           resumed_final_checksum)
    if (NOT name STREQUAL expected_name OR NOT tick STREQUAL expected_tick OR
        NOT resumed STREQUAL final_authoritative_checksum)
      message(FATAL_ERROR
        "${driver} checkpoint ${index} is not canonical: ${name}/${tick}/${resumed}")
    endif ()
  endforeach ()
  file(SHA256 "${snapshot}" snapshot_sha)
  set("${driver}_final" "${final_authoritative_checksum}" PARENT_SCOPE)
  set("${driver}_initial_frame"
      "${target_system_initial_framebuffer_checksum}" PARENT_SCOPE)
  set("${driver}_moved_frame"
      "${target_system_moved_framebuffer_checksum}" PARENT_SCOPE)
  set("${driver}_final_frame" "${framebuffer_checksum}" PARENT_SCOPE)
  set("${driver}_snapshot" "${snapshot_sha}" PARENT_SCOPE)
endfunction ()

check_intersystem_contract(ansi)
check_intersystem_contract(kitty)
if (NOT ansi_final STREQUAL kitty_final OR
    NOT ansi_initial_frame STREQUAL kitty_initial_frame OR
    NOT ansi_moved_frame STREQUAL kitty_moved_frame OR
    NOT ansi_final_frame STREQUAL kitty_final_frame OR
    NOT ansi_snapshot STREQUAL kitty_snapshot)
  message(FATAL_ERROR
    "Kitty and ANSI complete contract acceptance results diverged")
endif ()
