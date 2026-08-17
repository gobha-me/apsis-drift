if (NOT DEFINED APSIS_DRIFT_BIN OR NOT DEFINED REPORT_DIR)
  message(FATAL_ERROR "APSIS_DRIFT_BIN and REPORT_DIR are required")
endif ()

function(run_acceptance profile suffix output_variable)
  set(report "${REPORT_DIR}/planetfall-${profile}-${suffix}.json")
  execute_process(
    COMMAND "${APSIS_DRIFT_BIN}" --planetfall-acceptance
            --profile "${profile}" --report "${report}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
  )
  if (NOT result EQUAL 0)
    message(FATAL_ERROR
      "${profile} Planetfall acceptance failed (${result})\n"
      "stdout:\n${output}\nstderr:\n${error}")
  endif ()
  file(READ "${report}" json)
  set("${output_variable}" "${json}" PARENT_SCOPE)
endfunction()

function(check_report json profile width height)
  foreach(field schema_version scenario seed planet_generator_version
                terrain_generator_version planet_id planet_name
                simulation_hz command_count final_tick
                final_flight_checksum render_profile viewport_width
                viewport_height frames_per_stage stages)
    string(JSON value ERROR_VARIABLE json_error GET "${json}" "${field}")
    if (json_error)
      message(FATAL_ERROR
        "${profile} Planetfall report is missing ${field}: ${json_error}")
    endif ()
  endforeach()

  string(JSON schema_version GET "${json}" schema_version)
  string(JSON scenario GET "${json}" scenario)
  string(JSON seed GET "${json}" seed)
  string(JSON command_count GET "${json}" command_count)
  string(JSON final_tick GET "${json}" final_tick)
  string(JSON final_checksum GET "${json}" final_flight_checksum)
  string(JSON actual_profile GET "${json}" render_profile)
  string(JSON actual_width GET "${json}" viewport_width)
  string(JSON actual_height GET "${json}" viewport_height)
  string(JSON frames_per_stage GET "${json}" frames_per_stage)
  string(JSON stage_count LENGTH "${json}" stages)
  if (NOT schema_version EQUAL 1 OR
      NOT scenario STREQUAL "v0.3-planetfall" OR
      NOT seed EQUAL 42 OR
      NOT command_count EQUAL 4 OR
      NOT final_tick EQUAL 119360 OR
      NOT final_checksum STREQUAL "240775156608294234" OR
      NOT actual_profile STREQUAL "${profile}" OR
      NOT actual_width EQUAL width OR
      NOT actual_height EQUAL height OR
      NOT frames_per_stage EQUAL 60 OR
      NOT stage_count EQUAL 4)
    message(FATAL_ERROR
      "${profile} Planetfall report does not match the canonical path:\n${json}")
  endif ()

  set(expected_modes orbital atmospheric terrain-blend local-terrain)
  set(expected_regimes orbital atmospheric atmospheric terrain-flight)
  set(expected_ticks 0 4080 104826 119360)
  set(expected_flight_checksums
      16209989626150487226
      6791447656138722384
      2533444641327445206
      240775156608294234)
  math(EXPR last_stage "${stage_count} - 1")
  foreach(index RANGE 0 ${last_stage})
    list(GET expected_modes ${index} expected_mode)
    list(GET expected_regimes ${index} expected_regime)
    list(GET expected_ticks ${index} expected_tick)
    list(GET expected_flight_checksums ${index} expected_flight_checksum)
    string(JSON mode GET "${json}" stages ${index} presentation_mode)
    string(JSON regime GET "${json}" stages ${index} flight_regime)
    string(JSON tick GET "${json}" stages ${index} tick)
    string(JSON flight_checksum GET "${json}" stages ${index}
           flight_checksum)
    string(JSON framebuffer_checksum GET "${json}" stages ${index}
           framebuffer_checksum)
    string(JSON total_avg_ms GET "${json}" stages ${index} total_avg_ms)
    string(JSON total_p95_ms GET "${json}" stages ${index} total_p95_ms)
    if (NOT mode STREQUAL expected_mode OR
        NOT regime STREQUAL expected_regime OR
        NOT tick EQUAL expected_tick OR
        NOT flight_checksum STREQUAL expected_flight_checksum OR
        framebuffer_checksum STREQUAL "0" OR
        total_avg_ms LESS 0 OR total_p95_ms LESS 0)
      message(FATAL_ERROR
        "${profile} Planetfall stage ${index} is invalid:\n${json}")
    endif ()
  endforeach()
endfunction()

foreach(profile remote local)
  run_acceptance("${profile}" first first_json)
  run_acceptance("${profile}" second second_json)
  if (profile STREQUAL "remote")
    check_report("${first_json}" "${profile}" 320 240)
    check_report("${second_json}" "${profile}" 320 240)
  else ()
    check_report("${first_json}" "${profile}" 640 480)
    check_report("${second_json}" "${profile}" 640 480)
  endif ()

  foreach(index RANGE 0 3)
    foreach(field presentation_mode flight_regime tick flight_checksum
                  framebuffer_checksum surface_anchor)
      string(JSON first_value GET "${first_json}" stages ${index} "${field}")
      string(JSON second_value GET "${second_json}" stages ${index} "${field}")
      if (NOT first_value STREQUAL second_value)
        message(FATAL_ERROR
          "${profile} Planetfall stage ${index} changed ${field} between runs")
      endif ()
    endforeach()
  endforeach()
endforeach()
