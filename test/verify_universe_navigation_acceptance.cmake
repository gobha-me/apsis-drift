if (NOT DEFINED APSIS_DRIFT_BIN)
  message(FATAL_ERROR "APSIS_DRIFT_BIN is required")
endif ()
if (NOT DEFINED REPORT_DIR)
  message(FATAL_ERROR "REPORT_DIR is required")
endif ()

set(report "${REPORT_DIR}/universe-navigation-contract.json")
execute_process(
  COMMAND "${APSIS_DRIFT_BIN}" --universe-navigation-acceptance
          --report "${report}"
  RESULT_VARIABLE result
  OUTPUT_QUIET
  ERROR_VARIABLE error
)
if (NOT result EQUAL 0)
  message(FATAL_ERROR
    "universe-navigation acceptance failed (${result})\nstderr:\n${error}")
endif ()

file(READ "${report}" json)
foreach(field schema_version scenario evidence_scope seed navigation_version
              route_seed origin_system_id destination_system_id axis
              distance_light_seconds distance_metres visible_rows
              selectable_rows ftl_total_ticks direct_cruise_distance_metres
              direct_speed_metres_per_second direct_duration_ticks
              maximum_time_scale maximum_scale_updates
              maximum_scale_realtime_milliseconds projected_save_bytes
              resource_cost_units direct_arrival_checksum
              application_renderer_budget_ms terminal_proxy_evidence)
  string(JSON value ERROR_VARIABLE json_error GET "${json}" "${field}")
  if (json_error)
    message(FATAL_ERROR
      "report field '${field}' failed to parse: ${json_error}")
  endif ()
  set("${field}" "${value}")
endforeach()

if (NOT schema_version STREQUAL "1" OR
    NOT scenario STREQUAL "v0.4.34-universe-navigation-contract" OR
    NOT evidence_scope STREQUAL "application_contract" OR
    NOT seed STREQUAL "42" OR
    NOT navigation_version STREQUAL "1" OR
    NOT route_seed STREQUAL "4490051804352235517" OR
    NOT origin_system_id STREQUAL "system-09683d79dbc20b52" OR
    NOT destination_system_id STREQUAL "system-28630482e6b15573" OR
    NOT axis STREQUAL "-z" OR
    NOT distance_light_seconds STREQUAL "321457" OR
    NOT distance_metres STREQUAL "96370384171306" OR
    NOT visible_rows STREQUAL "2" OR
    NOT selectable_rows STREQUAL "1" OR
    NOT ftl_total_ticks STREQUAL "600" OR
    NOT direct_cruise_distance_metres STREQUAL "96170384171306" OR
    NOT direct_speed_metres_per_second STREQUAL "1000000" OR
    NOT direct_duration_ticks STREQUAL "11540446101" OR
    NOT maximum_time_scale STREQUAL "65536" OR
    NOT maximum_scale_updates STREQUAL "176094" OR
    NOT maximum_scale_realtime_milliseconds STREQUAL "1467450" OR
    NOT projected_save_bytes STREQUAL "386" OR
    NOT resource_cost_units STREQUAL "0" OR
    NOT direct_arrival_checksum STREQUAL "7017212702108484702" OR
    NOT application_renderer_budget_ms STREQUAL "1.0" OR
    NOT terminal_proxy_evidence STREQUAL
        "separate-live-capture-required-by-contract-three")
  message(FATAL_ERROR
    "universe-navigation report is not canonical:\n${json}")
endif ()
