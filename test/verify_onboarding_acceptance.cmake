if (NOT DEFINED APSIS_DRIFT_BIN OR NOT DEFINED REPORT_DIR)
  message(FATAL_ERROR "APSIS_DRIFT_BIN and REPORT_DIR are required")
endif ()

function(run_onboarding driver profile output_variable)
  set(report "${REPORT_DIR}/onboarding-${driver}-${profile}.json")
  execute_process(
    COMMAND "${APSIS_DRIFT_BIN}" --onboarding-acceptance
            --driver "${driver}" --profile "${profile}" --report "${report}"
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_VARIABLE error
  )
  if (NOT result EQUAL 0)
    message(FATAL_ERROR
      "${driver}/${profile} onboarding failed (${result})\nstderr:\n${error}")
  endif ()
  file(READ "${report}" json)
  set("${output_variable}" "${json}" PARENT_SCOPE)
endfunction ()

function(check_onboarding driver profile json)
  foreach(field schema_version scenario evidence_scope presentation
                render_profile guided_new_game_verified
                skipped_new_game_verified free_flight_redock_verified
                pause_resume_verified pilot_recovery_verified
                pilot_recovery_checksum presentation_framebuffer_checksum
                encoded_bytes encoded_frames)
    string(JSON value ERROR_VARIABLE json_error GET "${json}" "${field}")
    if (json_error)
      message(FATAL_ERROR
        "${driver}/${profile} field '${field}' failed to parse: ${json_error}")
    endif ()
    set("${field}" "${value}")
  endforeach ()
  string(JSON seed_count LENGTH "${json}" seeds)
  if (NOT schema_version STREQUAL "1" OR
      NOT scenario STREQUAL "v0.4.38-station-to-universe-onboarding" OR
      NOT evidence_scope STREQUAL
          "application_state_framebuffer_and_encoder" OR
      NOT presentation STREQUAL "${driver}" OR
      NOT render_profile STREQUAL "${profile}" OR
      NOT guided_new_game_verified OR NOT skipped_new_game_verified OR
      NOT free_flight_redock_verified OR NOT pause_resume_verified OR
      NOT pilot_recovery_verified OR pilot_recovery_checksum STREQUAL "0" OR
      presentation_framebuffer_checksum STREQUAL "0" OR
      encoded_bytes STREQUAL "0" OR encoded_frames STREQUAL "0" OR
      NOT seed_count STREQUAL "3")
    message(FATAL_ERROR
      "${driver}/${profile} onboarding report is incomplete:\n${json}")
  endif ()

  set(expected_seeds 1 42 12648430)
  set(expected_stations station-3af25ee1d82c8326
                        station-ce51e866ec4e032d
                        station-3cdc671b528edb48)
  set(expected_home_planets planet-4df81d899eed3a63
                            planet-435b7b7e8ce489e8
                            planet-c7d2d403b1f8548d)
  set(expected_final_ticks 10807005 9955184 9313934)
  set(expected_guided_checksums 1768195985884066418
                                1264404524121391518
                                8813183419133451890)
  set(expected_skipped_checksums 5354591339519471185
                                 15500481623044795783
                                 8565292684381759337)
  foreach(index RANGE 0 2)
    list(GET expected_seeds ${index} expected_seed)
    list(GET expected_stations ${index} expected_station)
    list(GET expected_home_planets ${index} expected_home_planet)
    list(GET expected_final_ticks ${index} expected_final_tick)
    list(GET expected_guided_checksums ${index} expected_guided_checksum)
    list(GET expected_skipped_checksums ${index} expected_skipped_checksum)
    string(JSON actual_seed GET "${json}" seeds ${index} seed)
    string(JSON station GET "${json}" seeds ${index} origin_station_id)
    string(JSON home_planet GET "${json}" seeds ${index} home_planet_id)
    string(JSON final_tick GET "${json}" seeds ${index} final_tick)
    string(JSON checkpoint_count GET "${json}" seeds ${index}
           save_checkpoint_count)
    string(JSON guided_checksum GET "${json}" seeds ${index}
           guided_final_checksum)
    string(JSON skipped_checksum GET "${json}" seeds ${index}
           skipped_baseline_checksum)
    string(JSON discovery_count GET "${json}" seeds ${index}
           guided_discovery_count)
    string(JSON world_delta_count GET "${json}" seeds ${index}
           guided_world_delta_count)
    foreach(field immutable_identities_match open_exploration_available
                  skipped_history_empty post_onboarding_idle_stable)
      string(JSON condition GET "${json}" seeds ${index} "${field}")
      if (NOT condition)
        message(FATAL_ERROR
          "${driver}/${profile} seed ${expected_seed} failed ${field}")
      endif ()
    endforeach ()
    if (NOT actual_seed STREQUAL expected_seed OR
        NOT station STREQUAL expected_station OR
        NOT home_planet STREQUAL expected_home_planet OR
        NOT final_tick STREQUAL expected_final_tick OR
        checkpoint_count LESS 20 OR guided_checksum STREQUAL "0" OR
        skipped_checksum STREQUAL "0" OR
        guided_checksum STREQUAL skipped_checksum OR
        NOT guided_checksum STREQUAL expected_guided_checksum OR
        NOT skipped_checksum STREQUAL expected_skipped_checksum OR
        NOT discovery_count STREQUAL "3" OR
        NOT world_delta_count STREQUAL "3")
      message(FATAL_ERROR
        "${driver}/${profile} seed ${expected_seed} evidence is invalid")
    endif ()
  endforeach ()
endfunction ()

run_onboarding(kitty local kitty_json)
check_onboarding(kitty local "${kitty_json}")
run_onboarding(ansi remote ansi_json)
check_onboarding(ansi remote "${ansi_json}")

string(JSON kitty_recovery_checksum GET "${kitty_json}"
       pilot_recovery_checksum)
string(JSON ansi_recovery_checksum GET "${ansi_json}"
       pilot_recovery_checksum)
if (NOT kitty_recovery_checksum STREQUAL "15160466842829483543" OR
    NOT kitty_recovery_checksum STREQUAL ansi_recovery_checksum)
  message(FATAL_ERROR
    "driver/profile changed the pilot recovery checksum")
endif ()

foreach(index RANGE 0 2)
  foreach(field seed origin_station_id home_planet_id final_tick
                guided_final_checksum
                skipped_baseline_checksum guided_discovery_count
                guided_world_delta_count save_checkpoint_count
                immutable_identities_match open_exploration_available
                skipped_history_empty post_onboarding_idle_stable)
    string(JSON kitty_value GET "${kitty_json}" seeds ${index} "${field}")
    string(JSON ansi_value GET "${ansi_json}" seeds ${index} "${field}")
    if (NOT kitty_value STREQUAL ansi_value)
      message(FATAL_ERROR
        "driver/profile changed seed ${index} authoritative field ${field}")
    endif ()
  endforeach ()
endforeach ()
