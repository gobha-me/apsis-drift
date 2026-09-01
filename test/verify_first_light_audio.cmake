if (NOT EXISTS "${AUDITION_BIN}" OR NOT EXISTS "${FFMPEG_BIN}" OR
    NOT IS_DIRECTORY "${ASSET_DIR}" OR NOT IS_DIRECTORY "${REPORT_DIR}")
  message(FATAL_ERROR "First Light level verification inputs are missing")
endif ()

function(measure_audio path output_lufs output_peak)
  execute_process(
    COMMAND "${FFMPEG_BIN}" -hide_banner -nostats -i "${path}"
            -af loudnorm=print_format=json -f null -
    RESULT_VARIABLE result
    OUTPUT_VARIABLE standard_output
    ERROR_VARIABLE standard_error
  )
  if (NOT result EQUAL 0)
    message(FATAL_ERROR "ffmpeg could not measure ${path}: ${standard_error}")
  endif ()
  set(analysis "${standard_output}\n${standard_error}")
  string(REGEX MATCH
    "\"input_i\"[ \t]*:[ \t]*\"(-?[0-9]+\\.[0-9]+)\""
    loudness_match "${analysis}")
  set(loudness "${CMAKE_MATCH_1}")
  string(REGEX MATCH
    "\"input_tp\"[ \t]*:[ \t]*\"(-?[0-9]+\\.[0-9]+)\""
    peak_match "${analysis}")
  set(peak "${CMAKE_MATCH_1}")
  if (loudness STREQUAL "" OR peak STREQUAL "")
    message(FATAL_ERROR "ffmpeg did not report finite levels for ${path}")
  endif ()
  set(${output_lufs} "${loudness}" PARENT_SCOPE)
  set(${output_peak} "${peak}" PARENT_SCOPE)
endfunction()

set(music_wav "${REPORT_DIR}/first-light-level-music.wav")
set(music_report "${REPORT_DIR}/first-light-level-music.json")
execute_process(
  COMMAND "${AUDITION_BIN}" "${ASSET_DIR}" "${music_wav}"
          "${music_report}" --music-only
  RESULT_VARIABLE audition_result
)
if (NOT audition_result EQUAL 0)
  message(FATAL_ERROR "the production-path music audition render failed")
endif ()
measure_audio("${music_wav}" music_lufs music_peak)
if (music_lufs LESS -24.0 OR music_lufs GREATER -22.0 OR
    music_peak GREATER -3.0)
  message(FATAL_ERROR
    "music level is outside -23 +/-1 LUFS-I and -3 dBTP: "
    "${music_lufs} LUFS-I, ${music_peak} dBTP")
endif ()

set(sfx_names
  ui-navigate
  ui-confirm
  ui-reject
  comms-notice
  signal-lock
  signal-complete
)
foreach (name IN LISTS sfx_names)
  set(path "${ASSET_DIR}/sfx/${name}.wav")
  measure_audio("${path}" sfx_lufs sfx_peak)
  if (sfx_lufs LESS -21.0 OR sfx_lufs GREATER -19.0 OR
      sfx_peak GREATER -1.0)
    message(FATAL_ERROR
      "${name} is outside -20 +/-1 LUFS-I and -1 dBTP: "
      "${sfx_lufs} LUFS-I, ${sfx_peak} dBTP")
  endif ()
endforeach ()

message(STATUS
  "First Light levels passed: music ${music_lufs} LUFS-I, "
  "${music_peak} dBTP; all six SFX passed")
