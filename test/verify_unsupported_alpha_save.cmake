if(NOT DEFINED APSIS_DRIFT_BIN OR NOT DEFINED REPORT_DIR)
  message(FATAL_ERROR "APSIS_DRIFT_BIN and REPORT_DIR are required")
endif()

set(unsupported_save "${REPORT_DIR}/unsupported-alpha-save.json")
file(WRITE "${unsupported_save}"
  "{\"application\":\"apsis-drift\",\"format_version\":14}\n")
file(SHA256 "${unsupported_save}" before_checksum)

execute_process(
  COMMAND "${APSIS_DRIFT_BIN}" --load "${unsupported_save}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)

file(SHA256 "${unsupported_save}" after_checksum)
if(NOT result EQUAL 1)
  message(FATAL_ERROR
    "unsupported alpha save returned ${result}\nstdout:\n${output}\nstderr:\n${error}")
endif()
string(FIND "${error}" "${unsupported_save}" path_index)
string(FIND "${error}" "format-15 home-contract alpha reset" reset_index)
string(FIND "${error}" "source file was not modified" untouched_index)
if(path_index EQUAL -1 OR reset_index EQUAL -1 OR untouched_index EQUAL -1 OR
   NOT before_checksum STREQUAL after_checksum)
  message(FATAL_ERROR
    "unsupported alpha save diagnostic or source preservation failed:\n${error}")
endif()
