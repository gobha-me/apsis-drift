if (NOT DEFINED APSIS_DRIFT_BIN)
  message(FATAL_ERROR "APSIS_DRIFT_BIN is required")
endif ()
if (NOT DEFINED REPORT_DIR)
  message(FATAL_ERROR "REPORT_DIR is required")
endif ()

function(run_signal_run profile suffix output_variable)
  set(report "${REPORT_DIR}/signal-run-${profile}-${suffix}.json")
  execute_process(
    COMMAND "${APSIS_DRIFT_BIN}" --signal-run-acceptance
            --profile "${profile}" --report "${report}"
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_VARIABLE error
  )
  if (NOT result EQUAL 0)
    message(FATAL_ERROR
      "${profile} Signal Run failed (${result})\nstderr:\n${error}")
  endif ()
  file(READ "${report}" json)
  set("${output_variable}" "${json}" PARENT_SCOPE)
endfunction ()

function(check_signal_run profile)
  run_signal_run("${profile}" first first_json)
  run_signal_run("${profile}" second second_json)
  if (NOT first_json STREQUAL second_json)
    message(FATAL_ERROR
      "${profile} Signal Run did not reproduce exactly")
  endif ()

  foreach(field schema_version scenario seed station_id contract_id target_id launch_tick
                initial_distance_metres first_motion_tick
                orbital_acceleration_ticks orbital_braking_ticks
                peak_orbital_speed_metres_per_second
                atmospheric_tick terrain_tick reached_tick completion_tick
                orbital_return_tick resume_tick checkpoint_flight_checksum
                resumed_flight_checksum return_flight_checksum
                sun_generator_version checkpoint_sun
                checkpoint_framebuffer_checksum
                resumed_framebuffer_checksum framebuffer_checksum
                terrain_safety_probe_ticks
                terrain_safety_minimum_clearance_metres
                terrain_safety_flight_checksum discovery_count
                world_delta_count final_location final_objective
                final_onboarding_chapter save_checkpoints sun_cycle
                render_profile evidence_scope viewport_width viewport_height)
    string(JSON value ERROR_VARIABLE json_error GET "${first_json}" "${field}")
    if (json_error)
      message(FATAL_ERROR
        "${profile} Signal Run field '${field}' failed to parse: ${json_error}")
    endif ()
    set("${field}" "${value}")
  endforeach ()

  if (NOT schema_version STREQUAL "6" OR
      NOT scenario STREQUAL "v0.4.32-home-signal-run" OR
      NOT seed STREQUAL "42" OR
      NOT station_id STREQUAL "station-ce51e866ec4e032d" OR
      NOT contract_id STREQUAL "contract-b9e5a14a1d979f3a" OR
      NOT target_id STREQUAL "signal-71d4c959dcd64423" OR
      NOT launch_tick STREQUAL "1" OR
      NOT initial_distance_metres MATCHES "^12111816\\.65" OR
      NOT first_motion_tick STREQUAL "1" OR
      NOT orbital_acceleration_ticks STREQUAL "377" OR
      NOT orbital_braking_ticks STREQUAL "433" OR
      NOT peak_orbital_speed_metres_per_second MATCHES "^4472\\.13" OR
      NOT atmospheric_tick STREQUAL "594512" OR
      NOT terrain_tick STREQUAL "606368" OR
      NOT reached_tick STREQUAL "606799" OR
      NOT completion_tick STREQUAL "607218" OR
      NOT orbital_return_tick STREQUAL "621743" OR
      NOT resume_tick STREQUAL "600" OR
      NOT checkpoint_flight_checksum STREQUAL "14868992880006021767" OR
      NOT resumed_flight_checksum STREQUAL checkpoint_flight_checksum OR
      NOT sun_generator_version STREQUAL "1" OR
      NOT checkpoint_framebuffer_checksum STREQUAL
          resumed_framebuffer_checksum OR
      checkpoint_framebuffer_checksum STREQUAL "0" OR
      NOT return_flight_checksum STREQUAL "2518766494053420091" OR
      NOT framebuffer_checksum MATCHES "^[0-9]+$" OR
      framebuffer_checksum STREQUAL "0" OR
      NOT terrain_safety_probe_ticks STREQUAL "120000" OR
      NOT terrain_safety_minimum_clearance_metres MATCHES "^16[.]0" OR
      NOT terrain_safety_flight_checksum STREQUAL
          "10518261029416213796" OR
      NOT discovery_count STREQUAL "1" OR
      NOT world_delta_count STREQUAL "1" OR
      NOT final_location STREQUAL "docked_at_origin" OR
      NOT final_objective STREQUAL "turned_in" OR
      NOT final_onboarding_chapter STREQUAL "contract_two" OR
      NOT render_profile STREQUAL "${profile}" OR
      NOT evidence_scope STREQUAL "application_framebuffer")
    message(FATAL_ERROR
      "${profile} Signal Run report is not canonical:\n${first_json}")
  endif ()

  string(JSON checkpoint_count ERROR_VARIABLE checkpoint_error
         LENGTH "${first_json}" save_checkpoints)
  if (checkpoint_error OR NOT checkpoint_count STREQUAL "8")
    message(FATAL_ERROR
      "${profile} Signal Run save checkpoint matrix is incomplete: ${checkpoint_error}")
  endif ()
  set(expected_checkpoint_names
      docked station-flight orbital atmospheric terrain objective-complete
      ascent rendezvous)
  foreach(index RANGE 0 7)
    list(GET expected_checkpoint_names ${index} expected_name)
    string(JSON checkpoint_name GET "${first_json}" save_checkpoints ${index} name)
    string(JSON checkpoint_checksum GET "${first_json}" save_checkpoints ${index} save_checksum)
    if (NOT checkpoint_name STREQUAL expected_name OR
        NOT checkpoint_checksum MATCHES "^[0-9]+$" OR
        checkpoint_checksum STREQUAL "0")
      message(FATAL_ERROR
        "${profile} Signal Run save checkpoint ${index} is invalid:\n${first_json}")
    endif ()
  endforeach ()

  string(JSON sun_cycle_count ERROR_VARIABLE sun_cycle_error
         LENGTH "${first_json}" sun_cycle)
  if (sun_cycle_error OR NOT sun_cycle_count STREQUAL "3")
    message(FATAL_ERROR
      "${profile} sun-cycle matrix is incomplete: ${sun_cycle_error}")
  endif ()
  set(expected_sun_visibility visible planet_occluded reemerged)
  set(expected_sun_ticks 66800 72000 77200)
  set(sun_signature "")
  foreach(index RANGE 0 2)
    list(GET expected_sun_visibility ${index} expected_visibility)
    list(GET expected_sun_ticks ${index} expected_tick)
    string(JSON visibility GET "${first_json}" sun_cycle ${index} visibility)
    string(JSON tick GET "${first_json}" sun_cycle ${index} tick)
    string(JSON direction GET "${first_json}" sun_cycle ${index} direction)
    string(JSON sun_pixels GET "${first_json}" sun_cycle ${index} sun_pixels)
    string(JSON sun_framebuffer GET "${first_json}" sun_cycle ${index}
           framebuffer_checksum)
    if (NOT visibility STREQUAL expected_visibility OR
        NOT tick STREQUAL expected_tick OR
        sun_framebuffer STREQUAL "0" OR
        (visibility STREQUAL "planet_occluded" AND NOT sun_pixels EQUAL 0) OR
        (NOT visibility STREQUAL "planet_occluded" AND sun_pixels EQUAL 0))
      message(FATAL_ERROR
        "${profile} sun-cycle checkpoint ${index} is invalid:\n${first_json}")
    endif ()
    string(APPEND sun_signature "${visibility}:${tick}:${direction};")
  endforeach ()

  string(JSON scenario_count ERROR_VARIABLE scenario_error
         LENGTH "${first_json}" scenarios)
  if (scenario_error OR NOT scenario_count STREQUAL "4")
    message(FATAL_ERROR
      "${profile} Signal Run scenario matrix is incomplete: ${scenario_error}")
  endif ()
  set(expected_seeds 42 12648430 1 42)
  set(expected_profiles assisted assisted assisted pilot)
  set(expected_atmospheres temperate temperate temperate temperate)
  set(expected_atmosphere_ticks 594512 105520 223505 594512)
  set(expected_terrain_ticks 606368 117387 235365 607828)
  set(expected_return_checksums
      2518766494053420091
      15091984075692739583
      13215400513595090789
      15173339839730381309)
  foreach(index RANGE 0 3)
    list(GET expected_seeds ${index} expected_seed)
    list(GET expected_profiles ${index} expected_profile)
    list(GET expected_atmospheres ${index} expected_atmosphere)
    list(GET expected_atmosphere_ticks ${index} expected_atmosphere_tick)
    list(GET expected_terrain_ticks ${index} expected_terrain_tick)
    list(GET expected_return_checksums ${index} expected_return_checksum)
    foreach(field seed rule_profile atmosphere_class atmospheric_tick terrain_tick
                  minimum_clearance_metres atmospheric_framebuffer_checksum
                  return_flight_checksum peak_thermal_load_units
                  thermal_abort_observed)
      string(JSON scenario_${field} ERROR_VARIABLE scenario_field_error
             GET "${first_json}" scenarios ${index} ${field})
      if (scenario_field_error)
        message(FATAL_ERROR
          "${profile} Signal Run scenario ${index} field ${field}: ${scenario_field_error}")
      endif ()
    endforeach ()
    math(EXPR atmospheric_leg
         "${scenario_terrain_tick} - ${scenario_atmospheric_tick}")
    if (NOT scenario_seed STREQUAL expected_seed OR
        NOT scenario_rule_profile STREQUAL expected_profile OR
        NOT scenario_atmosphere_class STREQUAL expected_atmosphere OR
        NOT scenario_atmospheric_tick STREQUAL expected_atmosphere_tick OR
        NOT scenario_terrain_tick STREQUAL expected_terrain_tick OR
        atmospheric_leg GREATER 14400 OR
        scenario_minimum_clearance_metres LESS 16 OR
        NOT scenario_atmospheric_framebuffer_checksum MATCHES "^[0-9]+$" OR
        scenario_atmospheric_framebuffer_checksum STREQUAL "0" OR
        NOT scenario_peak_thermal_load_units MATCHES "^[0-9]+$" OR
        scenario_peak_thermal_load_units LESS 1 OR
        scenario_peak_thermal_load_units GREATER 1000000 OR
        NOT scenario_return_flight_checksum STREQUAL expected_return_checksum)
      message(FATAL_ERROR
        "${profile} Signal Run scenario ${index} is not canonical:\n${first_json}")
    endif ()
  endforeach ()

  set("${profile}_flight_checksum" "${return_flight_checksum}" PARENT_SCOPE)
  set("${profile}_completion_tick" "${completion_tick}" PARENT_SCOPE)
  set("${profile}_return_tick" "${orbital_return_tick}" PARENT_SCOPE)
  set("${profile}_safety_checksum" "${terrain_safety_flight_checksum}" PARENT_SCOPE)
  set("${profile}_sun_signature" "${sun_signature}" PARENT_SCOPE)
endfunction ()

check_signal_run(remote)
check_signal_run(local)

if (NOT remote_flight_checksum STREQUAL local_flight_checksum OR
    NOT remote_completion_tick STREQUAL local_completion_tick OR
    NOT remote_return_tick STREQUAL local_return_tick OR
    NOT remote_safety_checksum STREQUAL local_safety_checksum OR
    NOT remote_sun_signature STREQUAL local_sun_signature)
  message(FATAL_ERROR
    "render profile changed deterministic Signal Run state")
endif ()
