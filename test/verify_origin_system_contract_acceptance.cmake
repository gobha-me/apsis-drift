if (NOT DEFINED APSIS_DRIFT_BIN)
  message(FATAL_ERROR "APSIS_DRIFT_BIN is required")
endif ()
if (NOT DEFINED REPORT_DIR)
  message(FATAL_ERROR "REPORT_DIR is required")
endif ()

set(report "${REPORT_DIR}/origin-system-contract-application-framebuffer.json")
set(snapshot "${REPORT_DIR}/origin-system-contract-application-framebuffer.ppm")
execute_process(
  COMMAND "${APSIS_DRIFT_BIN}" --origin-system-contract-acceptance
          --profile remote --report "${report}" --snapshot "${snapshot}"
  RESULT_VARIABLE result
  OUTPUT_QUIET
  ERROR_VARIABLE error
)
if (NOT result EQUAL 0)
  message(FATAL_ERROR
    "origin-system contract acceptance failed (${result})\nstderr:\n${error}")
endif ()

file(READ "${report}" json)
foreach(field schema_version scenario evidence_scope seed system_id contract_id
              home_planet_id target_planet_id target_objective_id outbound_tick
              target_insertion_tick objective_tick return_tick rendezvous_tick
              final_tick outbound_checksum return_checksum
              final_station_checksum framebuffer_checksum)
  string(JSON value ERROR_VARIABLE json_error GET "${json}" "${field}")
  if (json_error)
    message(FATAL_ERROR
      "report field '${field}' failed to parse: ${json_error}")
  endif ()
  set("${field}" "${value}")
endforeach ()
string(JSON checkpoint_count LENGTH "${json}" checkpoints)
if (NOT schema_version STREQUAL "1" OR
    NOT scenario STREQUAL "v0.4.33-origin-system-transfer" OR
    NOT evidence_scope STREQUAL "application_framebuffer" OR
    NOT seed STREQUAL "42" OR
    NOT system_id STREQUAL "system-09683d79dbc20b52" OR
    NOT contract_id STREQUAL "mission-fb6e1bfcfc619600" OR
    NOT home_planet_id STREQUAL "planet-435b7b7e8ce489e8" OR
    NOT target_planet_id STREQUAL "planet-6256428797d3d409" OR
    NOT target_objective_id STREQUAL "signal-cf034f5cb24b2d87" OR
    NOT outbound_tick STREQUAL "1" OR
    NOT target_insertion_tick STREQUAL "2948802" OR
    NOT objective_tick STREQUAL "3992789" OR
    NOT return_tick STREQUAL "4008189" OR
    NOT rendezvous_tick STREQUAL "7490650" OR
    NOT final_tick STREQUAL "7535733" OR
    NOT outbound_checksum STREQUAL "3657848727488505603" OR
    NOT return_checksum STREQUAL "14827224438636223116" OR
    NOT final_station_checksum STREQUAL "18266861312407229512" OR
    NOT framebuffer_checksum STREQUAL "17760165974771931201" OR
    NOT checkpoint_count STREQUAL "7")
  message(FATAL_ERROR
    "origin-system contract report is not canonical:\n${json}")
endif ()

set(expected_names outbound-transfer time-scaled-cruise target-approach
                   objective-complete return-transfer station-rendezvous
                   turned-in)
set(expected_ticks 1 177 2948802 3992789 4008189 7490650 7535733)
set(expected_checksums 4787545303882689689 8054959108351542012
                       12636926290365763755 13134725707714180051
                       7358887864313126266 15841728111193741642
                       13725047851651995134)
math(EXPR final_index "${checkpoint_count} - 1")
foreach(index RANGE 0 ${final_index})
  list(GET expected_names ${index} expected_name)
  list(GET expected_ticks ${index} expected_tick)
  list(GET expected_checksums ${index} expected_checksum)
  string(JSON name GET "${json}" checkpoints ${index} name)
  string(JSON tick GET "${json}" checkpoints ${index} tick)
  string(JSON checksum GET "${json}" checkpoints ${index} save_checksum)
  if (NOT name STREQUAL expected_name OR NOT tick STREQUAL expected_tick OR
      NOT checksum STREQUAL expected_checksum)
    message(FATAL_ERROR
      "checkpoint ${index} is not canonical: ${name}/${tick}/${checksum}")
  endif ()
endforeach ()
file(SHA256 "${snapshot}" snapshot_sha)
