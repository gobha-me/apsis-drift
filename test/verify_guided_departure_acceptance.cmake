if (NOT DEFINED APSIS_DRIFT_BIN)
  message(FATAL_ERROR "APSIS_DRIFT_BIN is required")
endif ()
if (NOT DEFINED REPORT_DIR)
  message(FATAL_ERROR "REPORT_DIR is required")
endif ()

function(run_departure driver profile suffix output_variable)
  set(report
      "${REPORT_DIR}/guided-departure-${driver}-${profile}-${suffix}.json")
  execute_process(
    COMMAND "${APSIS_DRIFT_BIN}" --guided-departure-acceptance
            --driver "${driver}" --profile "${profile}" --report "${report}"
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_VARIABLE error
  )
  if (NOT result EQUAL 0)
    message(FATAL_ERROR
      "${driver}/${profile} guided departure failed (${result})\nstderr:\n${error}")
  endif ()
  file(READ "${report}" json)
  set("${output_variable}" "${json}" PARENT_SCOPE)
endfunction ()

function(check_departure driver profile)
  run_departure("${driver}" "${profile}" first first_json)
  run_departure("${driver}" "${profile}" second second_json)

  foreach(field schema_version scenario evidence_scope presentation
                render_profile launch_tick station_flight_checksum
                planetfall_flight_checksum framebuffer_checksum frames
                encoded_bytes)
    string(JSON value ERROR_VARIABLE json_error GET "${first_json}" "${field}")
    if (json_error)
      message(FATAL_ERROR
        "${driver}/${profile} field '${field}' failed to parse: ${json_error}")
    endif ()
    set("${field}" "${value}")
  endforeach ()
  if (NOT schema_version STREQUAL "1" OR
      NOT scenario STREQUAL "v0.4.37-guided-contract-one-departure" OR
      NOT evidence_scope STREQUAL "application_framebuffer_and_encoder" OR
      NOT presentation STREQUAL "${driver}" OR
      NOT render_profile STREQUAL "${profile}" OR
      NOT launch_tick MATCHES "^[0-9]+$" OR
      station_flight_checksum STREQUAL "0" OR
      planetfall_flight_checksum STREQUAL "0" OR
      framebuffer_checksum STREQUAL "0" OR
      NOT frames MATCHES "^[0-9]+$" OR frames STREQUAL "0" OR
      encoded_bytes STREQUAL "0")
    message(FATAL_ERROR
      "${driver}/${profile} guided departure report is invalid:\n${first_json}")
  endif ()

  string(JSON event_count ERROR_VARIABLE event_error LENGTH "${first_json}"
         events)
  if (event_error OR NOT event_count STREQUAL "3")
    message(FATAL_ERROR
      "${driver}/${profile} guided departure event trace is incomplete")
  endif ()
  set(expected_inputs enter w enter)
  set(expected_outcomes redocked left_docking_envelope planetfall_started)
  set(expected_screens station flight flight)
  foreach(index RANGE 0 2)
    list(GET expected_inputs ${index} expected_input)
    list(GET expected_outcomes ${index} expected_outcome)
    list(GET expected_screens ${index} expected_screen)
    string(JSON input GET "${first_json}" events ${index} input)
    string(JSON outcome GET "${first_json}" events ${index} outcome)
    string(JSON screen GET "${first_json}" events ${index} screen)
    if (NOT input STREQUAL expected_input OR
        NOT outcome STREQUAL expected_outcome OR
        NOT screen STREQUAL expected_screen)
      message(FATAL_ERROR
        "${driver}/${profile} event ${index} is invalid:\n${first_json}")
    endif ()
  endforeach ()
  string(JSON redock_running GET "${first_json}" events 0 process_running)
  string(JSON departure_distance GET "${first_json}" events 1 distance_metres)
  string(JSON planetfall_running GET "${first_json}" events 2 process_running)
  if (NOT redock_running OR NOT planetfall_running OR
      departure_distance LESS_EQUAL 5000)
    message(FATAL_ERROR
      "${driver}/${profile} did not prove the departure interaction contract")
  endif ()

  foreach(field events launch_tick station_flight_checksum
                planetfall_flight_checksum framebuffer_checksum frames)
    string(JSON first_value GET "${first_json}" "${field}")
    string(JSON second_value GET "${second_json}" "${field}")
    if (NOT first_value STREQUAL second_value)
      message(FATAL_ERROR
        "${driver}/${profile} authoritative field '${field}' did not reproduce")
    endif ()
  endforeach ()

  set("${driver}_${profile}_station" "${station_flight_checksum}" PARENT_SCOPE)
  set("${driver}_${profile}_planetfall" "${planetfall_flight_checksum}"
      PARENT_SCOPE)
  set("${driver}_${profile}_framebuffer" "${framebuffer_checksum}"
      PARENT_SCOPE)
endfunction ()

foreach(profile remote local)
  check_departure(kitty "${profile}")
  check_departure(ansi "${profile}")
  if (NOT kitty_${profile}_station STREQUAL ansi_${profile}_station OR
      NOT kitty_${profile}_planetfall STREQUAL ansi_${profile}_planetfall OR
      NOT kitty_${profile}_framebuffer STREQUAL ansi_${profile}_framebuffer)
    message(FATAL_ERROR
      "${profile} Kitty and ANSI paths changed authoritative evidence")
  endif ()
endforeach ()

if (NOT kitty_remote_station STREQUAL kitty_local_station OR
    NOT kitty_remote_planetfall STREQUAL kitty_local_planetfall)
  message(FATAL_ERROR
    "render profile changed deterministic guided departure state")
endif ()
