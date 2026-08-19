if(NOT DEFINED APSIS_DRIFT_BIN OR NOT DEFINED SOURCE_DIR OR
   NOT DEFINED REPORT_DIR)
  message(FATAL_ERROR "APSIS_DRIFT_BIN, SOURCE_DIR, and REPORT_DIR are required")
endif()

file(READ "${SOURCE_DIR}/test/data/save-v2-golden.json" save_json)
string(REPLACE
  "\"target_signal_id\": \"signal-71d4c959dcd64423\""
  "\"target_signal_id\": \"signal-0000000000000001\""
  save_json "${save_json}")
set(invalid_save "${REPORT_DIR}/invalid-legacy-signal-save.json")
file(WRITE "${invalid_save}" "${save_json}")

execute_process(
  COMMAND "${APSIS_DRIFT_BIN}" --load "${invalid_save}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)

if(NOT result EQUAL 1)
  message(FATAL_ERROR
    "invalid legacy save returned ${result}\nstdout:\n${output}\nstderr:\n${error}")
endif()
string(FIND "${error}" "${invalid_save}" path_index)
string(FIND "${error}"
  "$.state.first_objective.target_signal_id" schema_index)
string(FIND "${error}" "cannot hydrate Signal Run profile" hydrate_index)
if(path_index EQUAL -1 OR schema_index EQUAL -1 OR NOT hydrate_index EQUAL -1)
  message(FATAL_ERROR
    "legacy save diagnostic was not specific to the load boundary:\n${error}")
endif()
