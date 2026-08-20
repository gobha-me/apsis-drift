if (NOT DEFINED APSIS_DRIFT_BIN)
  message(FATAL_ERROR "APSIS_DRIFT_BIN is required")
endif ()
if (NOT DEFINED REPORT_DIR)
  message(FATAL_ERROR "REPORT_DIR is required")
endif ()

function(check_intersystem_contract)
  set(report "${REPORT_DIR}/intersystem-contract-application-framebuffer.json")
  set(snapshot "${REPORT_DIR}/intersystem-contract-application-framebuffer.ppm")
  execute_process(
    COMMAND "${APSIS_DRIFT_BIN}" --intersystem-contract-acceptance
            --profile remote --report "${report}"
            --snapshot "${snapshot}"
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_VARIABLE error
  )
  if (NOT result EQUAL 0)
    message(FATAL_ERROR
      "intersystem contract acceptance failed (${result})\nstderr:\n${error}")
  endif ()
  file(READ "${report}" json)
  foreach(field schema_version scenario evidence_scope seed mission_id
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
        "report field '${field}' failed to parse: ${json_error}")
    endif ()
    set("${field}" "${value}")
  endforeach ()
  string(JSON checkpoint_count LENGTH "${json}" checkpoints)
  if (NOT schema_version STREQUAL "2" OR
      NOT scenario STREQUAL "v0.4.29-origin-system-free-flight" OR
      NOT evidence_scope STREQUAL "application_framebuffer" OR
      NOT seed STREQUAL "42" OR
      NOT mission_id STREQUAL "mission-d8e068532886e95b" OR
      NOT target_system_id STREQUAL "system-28630482e6b15573" OR
      NOT target_planet_id STREQUAL "planet-a1dc72d8fd111fbb" OR
      NOT target_objective_id STREQUAL "signal-9936ac67f2245d20" OR
      NOT origin_station_id STREQUAL "station-ce51e866ec4e032d" OR
      NOT final_tick STREQUAL "32073" OR
      NOT final_mission_phase STREQUAL "turned_in" OR
      NOT final_authoritative_checksum STREQUAL "8088546214365816373" OR
      NOT wrong_side_recovery_checksum STREQUAL "15160466842829483543" OR
      NOT target_system_planet_count STREQUAL "6" OR
      NOT target_system_initial_framebuffer_checksum STREQUAL
          "13519396001762605819" OR
      NOT target_system_moved_framebuffer_checksum STREQUAL
          "5625436622452186767" OR
      NOT discovery_count STREQUAL "1" OR
      NOT world_delta_count STREQUAL "1" OR
      NOT framebuffer_checksum STREQUAL "14932622265651659303" OR
      NOT checkpoint_count STREQUAL "9")
    message(FATAL_ERROR
      "intersystem contract report is not canonical:\n${json}")
  endif ()
  set(expected_names docked origin-flight outbound-spool canceled-spool
                     outbound-transit target-system planet-side origin-return
                     returned-docked)
  set(expected_ticks 0 121 532 532 892 1132 9999 31004 32073)
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
        "checkpoint ${index} is not canonical: ${name}/${tick}/${resumed}")
    endif ()
  endforeach ()
  file(SHA256 "${snapshot}" snapshot_sha)
endfunction ()

check_intersystem_contract()
