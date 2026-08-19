if (NOT DEFINED APSIS_DRIFT_BIN)
  message(FATAL_ERROR "APSIS_DRIFT_BIN is required")
endif ()
if (NOT DEFINED REPORT_DIR)
  message(FATAL_ERROR "REPORT_DIR is required")
endif ()

set(initial_report "${REPORT_DIR}/fixed-step-initial.json")
set(high_precision_report "${REPORT_DIR}/fixed-step-high-precision.json")
set(first_report "${REPORT_DIR}/fixed-step-first.json")
set(second_report "${REPORT_DIR}/fixed-step-second.json")

function(run_benchmark frames report)
  execute_process(
    COMMAND "${APSIS_DRIFT_BIN}" --benchmark "${frames}" --profile remote
            --seed 12648430 --report "${report}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
  )
  if (NOT result EQUAL 0)
    message(FATAL_ERROR
      "benchmark failed (${result})\nstdout:\n${output}\nstderr:\n${error}")
  endif ()
endfunction()

run_benchmark(1 "${initial_report}")
run_benchmark(2 "${high_precision_report}")
run_benchmark(120 "${first_report}")
run_benchmark(120 "${second_report}")

file(READ "${initial_report}" initial_json)
file(READ "${high_precision_report}" high_precision_json)
file(READ "${first_report}" first_json)
file(READ "${second_report}" second_json)

foreach(prefix IN ITEMS initial high_precision first second)
  string(JSON schema_version GET "${${prefix}_json}" schema_version)
  string(JSON checksum_type TYPE "${${prefix}_json}" checksum)
  string(JSON total_bytes_type TYPE "${${prefix}_json}" total_bytes)
  string(JSON checksum GET "${${prefix}_json}" checksum)
  string(JSON total_bytes GET "${${prefix}_json}" total_bytes)
  if (NOT schema_version STREQUAL "1" OR
      NOT checksum_type STREQUAL "STRING" OR
      NOT total_bytes_type STREQUAL "STRING" OR
      NOT checksum MATCHES "^[0-9]+$" OR
      NOT total_bytes MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR
      "${prefix} benchmark report has an invalid lossless summary:\n${${prefix}_json}")
  endif ()
  set("${prefix}_checksum" "${checksum}")
endforeach()

set(expected_high_precision_checksum "17391222284792806772")
if (NOT high_precision_checksum STREQUAL expected_high_precision_checksum)
  message(FATAL_ERROR
    "two-frame benchmark checksum changed: ${high_precision_checksum}")
endif ()
if (initial_checksum STREQUAL first_checksum)
  message(FATAL_ERROR "headless simulation did not advance between frames")
endif ()
if (NOT first_checksum STREQUAL second_checksum)
  message(FATAL_ERROR "repeated headless simulation is not deterministic")
endif ()

find_program(NODE_BIN NAMES node nodejs REQUIRED)
execute_process(
  COMMAND "${NODE_BIN}" "${CMAKE_CURRENT_LIST_DIR}/verify_benchmark_json.js"
          "${high_precision_report}" "${expected_high_precision_checksum}"
  RESULT_VARIABLE node_result
  OUTPUT_VARIABLE node_output
  ERROR_VARIABLE node_error
)
if (NOT node_result EQUAL 0)
  message(FATAL_ERROR
    "JavaScript benchmark JSON verification failed (${node_result})\n"
    "stdout:\n${node_output}\nstderr:\n${node_error}")
endif ()
