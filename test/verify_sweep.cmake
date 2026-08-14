if (NOT DEFINED APSIS_DRIFT_BIN OR NOT DEFINED REPORT_DIR)
  message(FATAL_ERROR "APSIS_DRIFT_BIN and REPORT_DIR are required")
endif ()

set(first_report "${REPORT_DIR}/sweep-report-first.json")
set(second_report "${REPORT_DIR}/sweep-report-second.json")

foreach (report IN ITEMS "${first_report}" "${second_report}")
  execute_process(
    COMMAND "${APSIS_DRIFT_BIN}" --sweep 2 --seed 12648430
            --report "${report}"
    RESULT_VARIABLE sweep_result
    OUTPUT_VARIABLE sweep_output
    ERROR_VARIABLE sweep_error
  )
  if (NOT sweep_result EQUAL 0)
    message(FATAL_ERROR
      "sweep failed with ${sweep_result}: ${sweep_error}\n${sweep_output}")
  endif ()
  if (NOT sweep_output MATCHES "frames-per-viewport=2" OR
      NOT sweep_output MATCHES "remote" OR
      NOT sweep_output MATCHES "balanced" OR
      NOT sweep_output MATCHES "local")
    message(FATAL_ERROR "sweep table is missing required identity fields")
  endif ()
endforeach ()

file(READ "${first_report}" first_json)
file(READ "${second_report}" second_json)

string(JSON schema_version GET "${first_json}" schema_version)
string(JSON workload GET "${first_json}" workload)
string(JSON seed GET "${first_json}" seed)
string(JSON frame_count GET "${first_json}" frames_per_viewport)
string(JSON measurement_count LENGTH "${first_json}" measurements)
if (NOT schema_version EQUAL 1 OR
    NOT workload STREQUAL "voxel-landscape-rgba" OR
    NOT seed EQUAL 12648430 OR
    NOT frame_count EQUAL 2 OR
    NOT measurement_count EQUAL 3)
  message(FATAL_ERROR "sweep report root structure is invalid")
endif ()

set(expected_profiles remote balanced local)
set(expected_widths 320 512 640)
set(expected_heights 240 320 480)
foreach (index RANGE 0 2)
  list(GET expected_profiles ${index} expected_profile)
  list(GET expected_widths ${index} expected_width)
  list(GET expected_heights ${index} expected_height)
  string(JSON profile GET "${first_json}" measurements ${index}
         render_profile)
  string(JSON width GET "${first_json}" measurements ${index}
         viewport_width)
  string(JSON height GET "${first_json}" measurements ${index}
         viewport_height)
  string(JSON frames GET "${first_json}" measurements ${index}
         summary frames)
  string(JSON checksum_first GET "${first_json}" measurements ${index}
         summary checksum)
  string(JSON checksum_second GET "${second_json}" measurements ${index}
         summary checksum)
  string(JSON target_count LENGTH "${first_json}" measurements ${index}
         targets)
  string(JSON first_target GET "${first_json}" measurements ${index}
         targets 0 target_fps)
  string(JSON second_target GET "${first_json}" measurements ${index}
         targets 1 target_fps)
  string(JSON deadline_type TYPE "${first_json}" measurements ${index}
         targets 0 deadline_budget_ms)
  if (NOT profile STREQUAL expected_profile OR
      NOT width EQUAL expected_width OR
      NOT height EQUAL expected_height OR
      NOT frames EQUAL 2 OR
      NOT checksum_first STREQUAL checksum_second OR
      NOT target_count EQUAL 2 OR
      NOT first_target EQUAL 30 OR
      NOT second_target EQUAL 60 OR
      NOT deadline_type STREQUAL "NUMBER")
    message(FATAL_ERROR
      "sweep measurement ${index} is invalid or nondeterministic")
  endif ()
endforeach ()
