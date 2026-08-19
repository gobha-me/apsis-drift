if (NOT DEFINED APSIS_DRIFT_BIN)
  message(FATAL_ERROR "APSIS_DRIFT_BIN is required")
endif ()
if (NOT DEFINED REPORT_DIR)
  message(FATAL_ERROR "REPORT_DIR is required")
endif ()

function(check_intersystem_planetfall)
  set(report "${REPORT_DIR}/intersystem-planetfall-application-framebuffer.json")
  set(snapshot "${REPORT_DIR}/intersystem-planetfall-application-framebuffer.ppm")
  execute_process(
    COMMAND "${APSIS_DRIFT_BIN}" --intersystem-planetfall-acceptance
            --profile remote --report "${report}"
            --snapshot "${snapshot}"
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_VARIABLE error
  )
  if (NOT result EQUAL 0)
    message(FATAL_ERROR
      "intersystem Planetfall acceptance failed (${result})\nstderr:\n${error}")
  endif ()
  file(READ "${report}" json)
  foreach(field schema_version scenario evidence_scope planet_id target_id
                abort_orbit_tick abort_orbit_checksum completion_tick
                completed_flight_checksum world_delta_count framebuffer_checksum)
    string(JSON value ERROR_VARIABLE json_error GET "${json}" "${field}")
    if (json_error)
      message(FATAL_ERROR
        "report field '${field}' failed to parse: ${json_error}")
    endif ()
    set("${field}" "${value}")
  endforeach ()
  foreach(field universe_seed planet_id nominal_peak_load_units shallow_peak_load_units
                manual_correction_peak_load_units assisted_peak_load_units
                forced_abort_tick recovery_orbit_tick deliberate_reentry_tick
                resumed_recovery_checksum)
    string(JSON thermal_${field} ERROR_VARIABLE json_error
      GET "${json}" thermal "${field}")
    if (json_error)
      message(FATAL_ERROR
        "thermal field '${field}' failed to parse: ${json_error}")
    endif ()
  endforeach ()
  string(JSON entry_count LENGTH "${json}" entries)
  foreach(index RANGE 0 2)
    string(JSON entry_name_${index} GET "${json}" entries ${index} name)
    string(JSON entry_tick_${index} GET "${json}" entries ${index} terrain_tick)
    string(JSON entry_checksum_${index} GET "${json}" entries ${index} flight_checksum)
  endforeach ()
  if (NOT schema_version STREQUAL "3" OR
      NOT scenario STREQUAL "v0.4.17-pilot-thermal-reentry" OR
      NOT evidence_scope STREQUAL "application_framebuffer" OR
      NOT planet_id STREQUAL "planet-a1dc72d8fd111fbb" OR
      NOT target_id STREQUAL "signal-9936ac67f2245d20" OR
      NOT entry_count STREQUAL "3" OR
      NOT entry_name_0 STREQUAL "correct-side" OR
      NOT entry_tick_0 STREQUAL "13526" OR
      NOT entry_checksum_0 STREQUAL "3373146089180912027" OR
      NOT entry_name_1 STREQUAL "early" OR
      NOT entry_tick_1 STREQUAL "13705" OR
      NOT entry_checksum_1 STREQUAL "7216854357076586787" OR
      NOT entry_name_2 STREQUAL "opposite-side" OR
      NOT entry_tick_2 STREQUAL "13934" OR
      NOT entry_checksum_2 STREQUAL "5891354715947044230" OR
      NOT abort_orbit_tick STREQUAL "3577" OR
      NOT abort_orbit_checksum STREQUAL "15160466842829483543" OR
      NOT completion_tick STREQUAL "1020" OR
      NOT completed_flight_checksum STREQUAL "1601798018321500146" OR
      NOT world_delta_count STREQUAL "1" OR
      NOT thermal_universe_seed STREQUAL "39" OR
      NOT thermal_planet_id STREQUAL "planet-237709a6a1fd198b" OR
      NOT thermal_nominal_peak_load_units STREQUAL "374" OR
      NOT thermal_shallow_peak_load_units STREQUAL "58770" OR
      NOT thermal_manual_correction_peak_load_units STREQUAL "35130" OR
      NOT thermal_assisted_peak_load_units STREQUAL "1000000" OR
      NOT thermal_forced_abort_tick STREQUAL "803" OR
      NOT thermal_recovery_orbit_tick STREQUAL "11764" OR
      NOT thermal_deliberate_reentry_tick STREQUAL "13108" OR
      NOT thermal_resumed_recovery_checksum STREQUAL
          "12793732928174323102" OR
      NOT framebuffer_checksum STREQUAL "15634582835738947125")
    message(FATAL_ERROR
      "intersystem Planetfall report is not canonical:\n${json}")
  endif ()
  file(SHA256 "${snapshot}" snapshot_sha)
endfunction ()

check_intersystem_planetfall()
