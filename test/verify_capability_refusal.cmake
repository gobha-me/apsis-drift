find_program(SCRIPT_BIN script REQUIRED)

function(check_refusal name arguments expected)
  execute_process(
    COMMAND "${SCRIPT_BIN}" --quiet --return --command
            "${APSIS_DRIFT_BIN} ${arguments}" /dev/null
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
  )
  set(combined "${output}${error}")
  if (NOT result EQUAL 1)
    message(FATAL_ERROR "${name}: expected exit 1, got ${result}: ${combined}")
  endif ()
  if (NOT combined MATCHES "${expected}")
    message(FATAL_ERROR "${name}: missing diagnostic '${expected}': ${combined}")
  endif ()
  string(ASCII 27 escape)
  string(FIND "${combined}" "${escape}[?1049h" alternate_screen)
  if (NOT alternate_screen EQUAL -1)
    message(FATAL_ERROR "${name}: entered the alternate screen before refusal")
  endif ()
endfunction()

check_refusal(
  "missing truecolor"
  "--driver fallback"
  "requires truecolor"
)
check_refusal(
  "missing key release"
  "--driver ansi --keyboard press-only"
  "requires complete key repeat events"
)
