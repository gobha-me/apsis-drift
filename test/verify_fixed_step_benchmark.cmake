if (NOT DEFINED APSIS_DRIFT_BIN)
  message(FATAL_ERROR "APSIS_DRIFT_BIN is required")
endif ()
if (NOT DEFINED REPORT_DIR)
  message(FATAL_ERROR "REPORT_DIR is required")
endif ()

set(initial_report "${REPORT_DIR}/fixed-step-initial.json")
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
run_benchmark(120 "${first_report}")
run_benchmark(120 "${second_report}")

file(READ "${initial_report}" initial_json)
file(READ "${first_report}" first_json)
file(READ "${second_report}" second_json)
string(REGEX MATCH "\"checksum\": ([0-9]+)" _ "${initial_json}")
set(initial_checksum "${CMAKE_MATCH_1}")
string(REGEX MATCH "\"checksum\": ([0-9]+)" _ "${first_json}")
set(first_checksum "${CMAKE_MATCH_1}")
string(REGEX MATCH "\"checksum\": ([0-9]+)" _ "${second_json}")
set(second_checksum "${CMAKE_MATCH_1}")

if (initial_checksum STREQUAL "" OR first_checksum STREQUAL "" OR
    second_checksum STREQUAL "")
  message(FATAL_ERROR "benchmark report is missing a checksum")
endif ()
if (initial_checksum STREQUAL first_checksum)
  message(FATAL_ERROR "headless simulation did not advance between frames")
endif ()
if (NOT first_checksum STREQUAL second_checksum)
  message(FATAL_ERROR "repeated headless simulation is not deterministic")
endif ()
