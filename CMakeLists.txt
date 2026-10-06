cmake_minimum_required(VERSION 3.28)

project(apsis-drift VERSION 0.4.44 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
include(FetchContent)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/include/apsis_drift/version.hpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/apsis_drift/version.hpp"
  @ONLY
)

option(APSIS_DRIFT_TESTS "Build the Apsis Drift tests" ON)
option(APSIS_DRIFT_TERMINAL "Build the TermForge terminal frontend" ON)
option(APSIS_DRIFT_RTAUDIO "Build the RtAudio output backend" ON)
option(APSIS_DRIFT_MIDI_SPIKE
       "Build the isolated deterministic MIDI score research spike" OFF)
option(APSIS_DRIFT_GODOT_SPIKE
       "Build the isolated C++ world snapshot for the Godot evaluation" OFF)
option(APSIS_DRIFT_GODOT_LIVE
       "Build the optional experimental Godot live C++ adapter" OFF)
if (APSIS_DRIFT_GODOT_LIVE AND NOT APSIS_DRIFT_GODOT_SPIKE)
  message(FATAL_ERROR "APSIS_DRIFT_GODOT_LIVE requires APSIS_DRIFT_GODOT_SPIKE")
endif ()
set(APSIS_DRIFT_TERMFORGE_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../termforge"
    CACHE PATH "Path to a TermForge source checkout")
set(APSIS_DRIFT_TERMFORGE_GIT_TAG
    "v0.57.23"
    CACHE STRING "Pinned TermForge revision used by FetchContent")

if (APSIS_DRIFT_TERMINAL)
find_package(termforge 0.57.23 CONFIG QUIET)
if (NOT TARGET termforge::lib)
  set(termforge_TESTS OFF CACHE BOOL "" FORCE)
  set(termforge_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(termforge_BIN OFF CACHE BOOL "" FORCE)
  set(termforge_BENCH OFF CACHE BOOL "" FORCE)
  set(termforge_INSTALL OFF CACHE BOOL "" FORCE)

  set(APSIS_DRIFT_TERMFORGE_SOURCE_COMPATIBLE FALSE)
  if (EXISTS "${APSIS_DRIFT_TERMFORGE_SOURCE_DIR}/CMakeLists.txt")
    if (TERMFORGE_VERSION VERSION_GREATER_EQUAL 0.57.23)
      set(APSIS_DRIFT_TERMFORGE_SOURCE_COMPATIBLE TRUE)
    else ()
      find_package(Git QUIET)
      if (GIT_FOUND)
        execute_process(
          COMMAND "${GIT_EXECUTABLE}" -C
                  "${APSIS_DRIFT_TERMFORGE_SOURCE_DIR}"
                  merge-base --is-ancestor v0.57.23 HEAD
          RESULT_VARIABLE APSIS_DRIFT_TERMFORGE_ANCESTOR_RESULT
          OUTPUT_QUIET
          ERROR_QUIET
        )
        if (APSIS_DRIFT_TERMFORGE_ANCESTOR_RESULT EQUAL 0)
          set(APSIS_DRIFT_TERMFORGE_SOURCE_COMPATIBLE TRUE)
        endif ()
      endif ()
    endif ()
  endif ()

  if (APSIS_DRIFT_TERMFORGE_SOURCE_COMPATIBLE)
    add_subdirectory(
      "${APSIS_DRIFT_TERMFORGE_SOURCE_DIR}"
      "${CMAKE_CURRENT_BINARY_DIR}/_deps/termforge-build"
      EXCLUDE_FROM_ALL
    )
  else ()
    if (EXISTS "${APSIS_DRIFT_TERMFORGE_SOURCE_DIR}/CMakeLists.txt")
      message(STATUS
        "Ignoring TermForge source checkout older than v0.57.23: "
        "${APSIS_DRIFT_TERMFORGE_SOURCE_DIR}")
    endif ()
    message(STATUS
      "TermForge not installed or found at the sibling path; fetching "
      "${APSIS_DRIFT_TERMFORGE_GIT_TAG}")
    FetchContent_Declare(termforge
      GIT_REPOSITORY https://github.com/gobha-me/termforge.git
      GIT_TAG "${APSIS_DRIFT_TERMFORGE_GIT_TAG}"
      GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(termforge)
  endif ()
endif ()
endif ()

find_package(nlohmann_json 3.12.0 CONFIG QUIET)
if (NOT TARGET nlohmann_json::nlohmann_json)
  set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
  set(JSON_Install OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH
      SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
  )
  FetchContent_MakeAvailable(nlohmann_json)
endif ()

if (APSIS_DRIFT_TERMINAL AND APSIS_DRIFT_RTAUDIO)
  set(RTAUDIO_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(RTAUDIO_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
  set(RTAUDIO_BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(RTAUDIO_BUILD_PYTHON OFF CACHE BOOL "" FORCE)
  set(RTAUDIO_API_JACK OFF CACHE BOOL "" FORCE)
  set(RTAUDIO_API_PULSE OFF CACHE BOOL "" FORCE)
  set(RTAUDIO_API_OSS OFF CACHE BOOL "" FORCE)
  set(RTAUDIO_API_DS OFF CACHE BOOL "" FORCE)
  set(RTAUDIO_API_ASIO OFF CACHE BOOL "" FORCE)
  if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(RTAUDIO_API_ALSA ON CACHE BOOL "" FORCE)
  elseif (APPLE)
    set(RTAUDIO_API_CORE ON CACHE BOOL "" FORCE)
  elseif (WIN32)
    set(RTAUDIO_API_WASAPI ON CACHE BOOL "" FORCE)
  endif ()
  FetchContent_Declare(rtaudio
    GIT_REPOSITORY https://github.com/thestk/rtaudio.git
    GIT_TAG b4f04903312e0e0efffbe77655172e0f060dc085
    GIT_SHALLOW FALSE
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(rtaudio)
endif ()

if (APSIS_DRIFT_TERMINAL)
FetchContent_Declare(tinysoundfont
  GIT_REPOSITORY https://github.com/schellingb/TinySoundFont.git
  GIT_TAG 853a0a171759f1ddba0de1442133a75912bbeffa
  GIT_SHALLOW FALSE
  EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(tinysoundfont)
endif ()

# Bounded source-bound station contact data is part of the application core;
# headless walking and saves never depend on a presentation-side physics world.
set(APSIS_ORIGIN_WALK_DATA "${CMAKE_CURRENT_SOURCE_DIR}/assets/native/origin-walk-01/geometry.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${APSIS_ORIGIN_WALK_DATA}")
file(SHA256 "${APSIS_ORIGIN_WALK_DATA}" APSIS_ORIGIN_WALK_SHA256)
if (NOT APSIS_ORIGIN_WALK_SHA256 STREQUAL "9cc9b5703ad53ff11871a21af707f9803c9a9c79067675ad6ff247a7950f7753")
  message(FATAL_ERROR "Origin walking contact geometry differs from the admitted version-one bytes")
endif ()
file(READ "${APSIS_ORIGIN_WALK_DATA}" APSIS_ORIGIN_WALK_JSON)
configure_file(include/apsis_drift/origin_walk_geometry.hpp.in
  "${CMAKE_CURRENT_BINARY_DIR}/generated/apsis_drift/origin_walk_geometry.hpp" @ONLY)
unset(APSIS_ORIGIN_WALK_JSON)

# The mechanism source policy is compiled from independently admitted bytes.
# Runtime parsing/evaluation cannot authorize a different source rig or curve.
set(APSIS_OPERATING_MOTION_DATA "${CMAKE_CURRENT_SOURCE_DIR}/assets/native/operating-motion-01/operating-motion-01.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${APSIS_OPERATING_MOTION_DATA}")
file(SHA256 "${APSIS_OPERATING_MOTION_DATA}" APSIS_OPERATING_MOTION_SHA256)
if (NOT APSIS_OPERATING_MOTION_SHA256 STREQUAL "afa1eb3d81deab1b5ac00a53d1650fa9bb222bcf8ea9c376efa166b93eda0298")
  message(FATAL_ERROR "Operating motion recipe differs from the admitted version-one bytes")
endif ()
file(READ "${APSIS_OPERATING_MOTION_DATA}" APSIS_OPERATING_MOTION_RECIPE_JSON)
configure_file(include/apsis_drift/operating_motion_recipe.hpp.in
  "${CMAKE_CURRENT_BINARY_DIR}/generated/apsis_drift/operating_motion_recipe.hpp" @ONLY)
unset(APSIS_OPERATING_MOTION_RECIPE_JSON)

# Exact source-bound support metadata is independent of submitted receipts.
set(APSIS_BOARDING_SUPPORT_DATA "${CMAKE_CURRENT_SOURCE_DIR}/assets/native/boarding-support-01/boarding-support-01.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${APSIS_BOARDING_SUPPORT_DATA}")
file(SHA256 "${APSIS_BOARDING_SUPPORT_DATA}" APSIS_BOARDING_SUPPORT_SHA256)
if (NOT APSIS_BOARDING_SUPPORT_SHA256 STREQUAL "58694e671102f27df5f405478af43d00fd05b3aeaef34c612939af6fa548a8d3")
  message(FATAL_ERROR "Boarding support differs from the admitted version-one bytes")
endif ()
file(READ "${APSIS_BOARDING_SUPPORT_DATA}" APSIS_BOARDING_SUPPORT_JSON)
string(LENGTH "${APSIS_BOARDING_SUPPORT_JSON}" APSIS_BOARDING_SUPPORT_BYTES)
set(APSIS_BOARDING_SUPPORT_CHUNKS "")
set(APSIS_BOARDING_SUPPORT_OFFSET 0)
while (APSIS_BOARDING_SUPPORT_OFFSET LESS APSIS_BOARDING_SUPPORT_BYTES)
  math(EXPR APSIS_BOARDING_SUPPORT_LENGTH "${APSIS_BOARDING_SUPPORT_BYTES} - ${APSIS_BOARDING_SUPPORT_OFFSET}")
  if (APSIS_BOARDING_SUPPORT_LENGTH GREATER 60000)
    set(APSIS_BOARDING_SUPPORT_LENGTH 60000)
  endif ()
  string(SUBSTRING "${APSIS_BOARDING_SUPPORT_JSON}" ${APSIS_BOARDING_SUPPORT_OFFSET} ${APSIS_BOARDING_SUPPORT_LENGTH} APSIS_BOARDING_SUPPORT_CHUNK)
  string(APPEND APSIS_BOARDING_SUPPORT_CHUNKS "    result.append(R\"APSISSUPPORT(${APSIS_BOARDING_SUPPORT_CHUNK})APSISSUPPORT\", ${APSIS_BOARDING_SUPPORT_LENGTH});\n")
  math(EXPR APSIS_BOARDING_SUPPORT_OFFSET "${APSIS_BOARDING_SUPPORT_OFFSET} + ${APSIS_BOARDING_SUPPORT_LENGTH}")
endwhile ()
configure_file(include/apsis_drift/boarding_support_data.hpp.in
  "${CMAKE_CURRENT_BINARY_DIR}/generated/apsis_drift/boarding_support_data.hpp" @ONLY)
unset(APSIS_BOARDING_SUPPORT_JSON)

# Full contact and extraction policy independently select source completeness.
set(APSIS_CABIN_CONTACT_DATA "${CMAKE_CURRENT_SOURCE_DIR}/assets/native/wayfarer-operating-02/metadata/contact.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${APSIS_CABIN_CONTACT_DATA}")
file(SHA256 "${APSIS_CABIN_CONTACT_DATA}" APSIS_CABIN_CONTACT_SHA256)
if (NOT APSIS_CABIN_CONTACT_SHA256 STREQUAL "109e3f140f6865612118b2712021a71c0732200f6adc14ac2d4a1ce1d749657a")
  message(FATAL_ERROR "Cabin full contact differs from admitted bytes")
endif ()
set(APSIS_CABIN_EXTRACTOR "${CMAKE_CURRENT_SOURCE_DIR}/assets/native/boarding-support-01/sources/tools/extract_boarding_support.py")
set(APSIS_CABIN_SOURCE_SUPPORT "${CMAKE_CURRENT_SOURCE_DIR}/assets/native/boarding-support-01/sources/wayfarer-boarding-support-01/support.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${APSIS_CABIN_EXTRACTOR}" "${APSIS_CABIN_SOURCE_SUPPORT}")
file(SHA256 "${APSIS_CABIN_EXTRACTOR}" APSIS_CABIN_EXTRACTOR_SHA256)
file(SHA256 "${APSIS_CABIN_SOURCE_SUPPORT}" APSIS_CABIN_SOURCE_SUPPORT_SHA256)
if (NOT APSIS_CABIN_EXTRACTOR_SHA256 STREQUAL "8173c9498e97b07af1813ec5645a38b9745505d0e3dadfb2edb7f52190cf97b6" OR
    NOT APSIS_CABIN_SOURCE_SUPPORT_SHA256 STREQUAL "2d84bf607ac136e8c847a5e7cd7be5529d9a28a89f01f444b520a0950cdaa5d2")
  message(FATAL_ERROR "Cabin fixed-crop/full-moving-source extraction policy differs")
endif ()
file(READ "${APSIS_CABIN_CONTACT_DATA}" APSIS_CABIN_CONTACT_JSON)
string(LENGTH "${APSIS_CABIN_CONTACT_JSON}" APSIS_CABIN_CONTACT_BYTES)
set(APSIS_CABIN_CONTACT_CHUNKS "")
set(APSIS_CABIN_CONTACT_OFFSET 0)
set(APSIS_CABIN_CONTACT_CHUNK_COUNT 0)
while (APSIS_CABIN_CONTACT_OFFSET LESS APSIS_CABIN_CONTACT_BYTES)
  math(EXPR APSIS_CABIN_CONTACT_LENGTH "${APSIS_CABIN_CONTACT_BYTES} - ${APSIS_CABIN_CONTACT_OFFSET}")
  if (APSIS_CABIN_CONTACT_LENGTH GREATER 60000)
    set(APSIS_CABIN_CONTACT_LENGTH 60000)
  endif ()
  string(SUBSTRING "${APSIS_CABIN_CONTACT_JSON}" ${APSIS_CABIN_CONTACT_OFFSET} ${APSIS_CABIN_CONTACT_LENGTH} APSIS_CABIN_CONTACT_CHUNK)
  string(APPEND APSIS_CABIN_CONTACT_CHUNKS "  std::string_view{R\"APSISCABIN(${APSIS_CABIN_CONTACT_CHUNK})APSISCABIN\", ${APSIS_CABIN_CONTACT_LENGTH}},\n")
  math(EXPR APSIS_CABIN_CONTACT_OFFSET "${APSIS_CABIN_CONTACT_OFFSET} + ${APSIS_CABIN_CONTACT_LENGTH}")
  math(EXPR APSIS_CABIN_CONTACT_CHUNK_COUNT "${APSIS_CABIN_CONTACT_CHUNK_COUNT} + 1")
endwhile ()
configure_file(include/apsis_drift/cabin_contact_data.hpp.in
  "${CMAKE_CURRENT_BINARY_DIR}/generated/apsis_drift/cabin_contact_data.hpp" @ONLY)
unset(APSIS_CABIN_CONTACT_JSON)

# Additive static source geometry has its own independent admission boundary;
# the original contact/support/motion bytes and seam coverage stay unchanged.
set(APSIS_LOWER_CONTACT_DATA "${CMAKE_CURRENT_SOURCE_DIR}/assets/native/lower-cockpit-contact-01/lower-cockpit-contact-01.json")
set(APSIS_LOWER_CONTACT_POLICY "${CMAKE_CURRENT_SOURCE_DIR}/assets/native/lower-cockpit-contact-01/contact-policy.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${APSIS_LOWER_CONTACT_DATA}" "${APSIS_LOWER_CONTACT_POLICY}")
file(SHA256 "${APSIS_LOWER_CONTACT_DATA}" APSIS_LOWER_CONTACT_SHA256)
file(SHA256 "${APSIS_LOWER_CONTACT_POLICY}" APSIS_LOWER_CONTACT_POLICY_SHA256)
if (NOT APSIS_LOWER_CONTACT_SHA256 STREQUAL "31407a19a36d45eb43f318d83f4bc362c837e7e2bf4b92c332f1444f4fcb7c42" OR
    NOT APSIS_LOWER_CONTACT_POLICY_SHA256 STREQUAL "da32508e5b8b062ba622576d8c3ce23828b2f119235542ece1cc66ffa73d7020")
  message(FATAL_ERROR "Lower cockpit geometry or independent source policy differs from admitted bytes")
endif ()
file(READ "${APSIS_LOWER_CONTACT_DATA}" APSIS_LOWER_CONTACT_JSON)
string(LENGTH "${APSIS_LOWER_CONTACT_JSON}" APSIS_LOWER_CONTACT_BYTES)
set(APSIS_LOWER_CONTACT_CHUNKS "")
set(APSIS_LOWER_CONTACT_OFFSET 0)
set(APSIS_LOWER_CONTACT_CHUNK_COUNT 0)
while (APSIS_LOWER_CONTACT_OFFSET LESS APSIS_LOWER_CONTACT_BYTES)
  math(EXPR APSIS_LOWER_CONTACT_LENGTH "${APSIS_LOWER_CONTACT_BYTES} - ${APSIS_LOWER_CONTACT_OFFSET}")
  if (APSIS_LOWER_CONTACT_LENGTH GREATER 60000)
    set(APSIS_LOWER_CONTACT_LENGTH 60000)
  endif ()
  string(SUBSTRING "${APSIS_LOWER_CONTACT_JSON}" ${APSIS_LOWER_CONTACT_OFFSET} ${APSIS_LOWER_CONTACT_LENGTH} APSIS_LOWER_CONTACT_CHUNK)
  string(APPEND APSIS_LOWER_CONTACT_CHUNKS "  std::string_view{R\"APSISLOWER(${APSIS_LOWER_CONTACT_CHUNK})APSISLOWER\", ${APSIS_LOWER_CONTACT_LENGTH}},\n")
  math(EXPR APSIS_LOWER_CONTACT_OFFSET "${APSIS_LOWER_CONTACT_OFFSET} + ${APSIS_LOWER_CONTACT_LENGTH}")
  math(EXPR APSIS_LOWER_CONTACT_CHUNK_COUNT "${APSIS_LOWER_CONTACT_CHUNK_COUNT} + 1")
endwhile ()
file(READ "${APSIS_LOWER_CONTACT_POLICY}" APSIS_LOWER_CONTACT_POLICY_JSON)
string(LENGTH "${APSIS_LOWER_CONTACT_POLICY_JSON}" APSIS_LOWER_CONTACT_POLICY_BYTES)
configure_file(include/apsis_drift/lower_cockpit_contact_data.hpp.in
  "${CMAKE_CURRENT_BINARY_DIR}/generated/apsis_drift/lower_cockpit_contact_data.hpp" @ONLY)
unset(APSIS_LOWER_CONTACT_JSON)
unset(APSIS_LOWER_CONTACT_POLICY_JSON)

# Exact offline-prepared stowed derivatives extend the existing contact handle.
# They do not select live presentation, actor equipment or a new save phase.
set(APSIS_STOWED_CONTACT_DATA "${CMAKE_CURRENT_SOURCE_DIR}/assets/native/stowed-cockpit-contact-01/replacement-contact.json")
set(APSIS_STOWED_FRAME_DATA "${CMAKE_CURRENT_SOURCE_DIR}/assets/native/stowed-cockpit-contact-01/frame.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${APSIS_STOWED_CONTACT_DATA}" "${APSIS_STOWED_FRAME_DATA}")
file(SHA256 "${APSIS_STOWED_CONTACT_DATA}" APSIS_STOWED_CONTACT_SHA256)
file(SHA256 "${APSIS_STOWED_FRAME_DATA}" APSIS_STOWED_FRAME_SHA256)
if (NOT APSIS_STOWED_CONTACT_SHA256 STREQUAL "62b4d493f2d74f59b81fd089873360b99e3bae9d17a6d90a00066e9440e44c4a" OR
    NOT APSIS_STOWED_FRAME_SHA256 STREQUAL "f98c50f69d71ecd38ca1d43d010f7b43ca300178dc25d40e170fd45fe5bc88a6")
  message(FATAL_ERROR "Stowed contact or frame differs from independently prepared bytes")
endif ()
file(READ "${APSIS_STOWED_CONTACT_DATA}" APSIS_STOWED_CONTACT_JSON)
string(LENGTH "${APSIS_STOWED_CONTACT_JSON}" APSIS_STOWED_CONTACT_BYTES)
set(APSIS_STOWED_CONTACT_CHUNKS "")
set(APSIS_STOWED_CONTACT_OFFSET 0)
set(APSIS_STOWED_CONTACT_CHUNK_COUNT 0)
while (APSIS_STOWED_CONTACT_OFFSET LESS APSIS_STOWED_CONTACT_BYTES)
  math(EXPR APSIS_STOWED_CONTACT_LENGTH "${APSIS_STOWED_CONTACT_BYTES} - ${APSIS_STOWED_CONTACT_OFFSET}")
  if (APSIS_STOWED_CONTACT_LENGTH GREATER 60000)
    set(APSIS_STOWED_CONTACT_LENGTH 60000)
  endif ()
  string(SUBSTRING "${APSIS_STOWED_CONTACT_JSON}" ${APSIS_STOWED_CONTACT_OFFSET} ${APSIS_STOWED_CONTACT_LENGTH} APSIS_STOWED_CONTACT_CHUNK)
  string(APPEND APSIS_STOWED_CONTACT_CHUNKS "  std::string_view{R\"APSISSTOW(${APSIS_STOWED_CONTACT_CHUNK})APSISSTOW\", ${APSIS_STOWED_CONTACT_LENGTH}},\n")
  math(EXPR APSIS_STOWED_CONTACT_OFFSET "${APSIS_STOWED_CONTACT_OFFSET} + ${APSIS_STOWED_CONTACT_LENGTH}")
  math(EXPR APSIS_STOWED_CONTACT_CHUNK_COUNT "${APSIS_STOWED_CONTACT_CHUNK_COUNT} + 1")
endwhile ()
file(READ "${APSIS_STOWED_FRAME_DATA}" APSIS_STOWED_FRAME_JSON)
string(LENGTH "${APSIS_STOWED_FRAME_JSON}" APSIS_STOWED_FRAME_BYTES)
configure_file(include/apsis_drift/stowed_contact_data.hpp.in
  "${CMAKE_CURRENT_BINARY_DIR}/generated/apsis_drift/stowed_contact_data.hpp" @ONLY)
unset(APSIS_STOWED_CONTACT_JSON)
unset(APSIS_STOWED_FRAME_JSON)

# Complete material inputs are prepared into one immutable translation unit.
# The preparer authenticates exact bytes; no authoring master is a build input.
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
set(APSIS_MATERIAL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/native/wayfarer-material-source-01")
set(APSIS_MATERIAL_PREPARER "${CMAKE_CURRENT_SOURCE_DIR}/tools/boarding/prepare_material_source.py")
set(APSIS_MATERIAL_PREPARED "${CMAKE_CURRENT_BINARY_DIR}/generated/origin_boarding_initial_material_prepared.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${APSIS_MATERIAL_SOURCE_DIR}/completion.json"
  "${APSIS_MATERIAL_SOURCE_DIR}/geometry.bin"
  "${APSIS_MATERIAL_PREPARER}")
execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${APSIS_MATERIAL_PREPARER}"
    "${APSIS_MATERIAL_SOURCE_DIR}/completion.json"
    "${APSIS_MATERIAL_SOURCE_DIR}/geometry.bin"
    "${APSIS_MATERIAL_PREPARED}"
  RESULT_VARIABLE APSIS_MATERIAL_PREPARATION_RESULT
  OUTPUT_VARIABLE APSIS_MATERIAL_PREPARATION_OUTPUT
  ERROR_VARIABLE APSIS_MATERIAL_PREPARATION_ERROR)
if (NOT APSIS_MATERIAL_PREPARATION_RESULT EQUAL 0)
  message(FATAL_ERROR "Complete material source preparation failed: ${APSIS_MATERIAL_PREPARATION_ERROR}")
endif ()

# Additive two-source completion: immutable public package only.
set(APSIS_CHECKPOINT_MATERIAL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/native/wayfarer-checkpoint-material-extension-01")
set(APSIS_CHECKPOINT_MATERIAL_PREPARER "${CMAKE_CURRENT_SOURCE_DIR}/tools/boarding/prepare_checkpoint_material_extension.py")
set(APSIS_CHECKPOINT_MATERIAL_PREPARED "${CMAKE_CURRENT_BINARY_DIR}/generated/origin_boarding_checkpoint_material_extension_prepared.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${APSIS_CHECKPOINT_MATERIAL_SOURCE_DIR}/completion.json"
  "${APSIS_CHECKPOINT_MATERIAL_SOURCE_DIR}/geometry.bin"
  "${APSIS_CHECKPOINT_MATERIAL_PREPARER}")
execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${APSIS_CHECKPOINT_MATERIAL_PREPARER}"
    "${APSIS_CHECKPOINT_MATERIAL_SOURCE_DIR}/completion.json"
    "${APSIS_CHECKPOINT_MATERIAL_SOURCE_DIR}/geometry.bin"
    "${APSIS_CHECKPOINT_MATERIAL_PREPARED}"
  RESULT_VARIABLE APSIS_CHECKPOINT_MATERIAL_PREPARATION_RESULT
  OUTPUT_VARIABLE APSIS_CHECKPOINT_MATERIAL_PREPARATION_OUTPUT
  ERROR_VARIABLE APSIS_CHECKPOINT_MATERIAL_PREPARATION_ERROR)
if (NOT APSIS_CHECKPOINT_MATERIAL_PREPARATION_RESULT EQUAL 0)
  message(FATAL_ERROR "Checkpoint material extension preparation failed: ${APSIS_CHECKPOINT_MATERIAL_PREPARATION_ERROR}")
endif ()

# Additive original hatch-seal material; no private Blender path is a dependency.
set(APSIS_HATCH_SEAL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/native/wayfarer-hatch-seal-material-01")
set(APSIS_HATCH_SEAL_PREPARER "${CMAKE_CURRENT_SOURCE_DIR}/tools/boarding/prepare_hatch_seal_material.py")
set(APSIS_HATCH_SEAL_PREPARED "${CMAKE_CURRENT_BINARY_DIR}/generated/origin_boarding_hatch_seal_material_prepared.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${APSIS_HATCH_SEAL_SOURCE_DIR}/completion.json"
  "${APSIS_HATCH_SEAL_SOURCE_DIR}/geometry.bin"
  "${APSIS_HATCH_SEAL_PREPARER}")
execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${APSIS_HATCH_SEAL_PREPARER}"
    "${APSIS_HATCH_SEAL_SOURCE_DIR}/completion.json"
    "${APSIS_HATCH_SEAL_SOURCE_DIR}/geometry.bin"
    "${APSIS_HATCH_SEAL_PREPARED}"
  RESULT_VARIABLE APSIS_HATCH_SEAL_PREPARATION_RESULT
  OUTPUT_VARIABLE APSIS_HATCH_SEAL_PREPARATION_OUTPUT
  ERROR_VARIABLE APSIS_HATCH_SEAL_PREPARATION_ERROR)
if (NOT APSIS_HATCH_SEAL_PREPARATION_RESULT EQUAL 0)
  message(FATAL_ERROR "Hatch-seal material preparation failed: ${APSIS_HATCH_SEAL_PREPARATION_ERROR}")
endif ()

# Additive original separation-ring material; no private Blender path is a dependency.
set(APSIS_SEPARATION_RING_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/native/wayfarer-separation-ring-material-01")
set(APSIS_SEPARATION_RING_PREPARER "${CMAKE_CURRENT_SOURCE_DIR}/tools/boarding/prepare_separation_ring_material.py")
set(APSIS_SEPARATION_RING_PREPARED "${CMAKE_CURRENT_BINARY_DIR}/generated/origin_boarding_separation_ring_material_prepared.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${APSIS_SEPARATION_RING_SOURCE_DIR}/completion.json"
  "${APSIS_SEPARATION_RING_SOURCE_DIR}/geometry.bin"
  "${APSIS_SEPARATION_RING_PREPARER}")
execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${APSIS_SEPARATION_RING_PREPARER}"
    "${APSIS_SEPARATION_RING_SOURCE_DIR}/completion.json"
    "${APSIS_SEPARATION_RING_SOURCE_DIR}/geometry.bin"
    "${APSIS_SEPARATION_RING_PREPARED}"
  RESULT_VARIABLE APSIS_SEPARATION_RING_PREPARATION_RESULT
  OUTPUT_VARIABLE APSIS_SEPARATION_RING_PREPARATION_OUTPUT
  ERROR_VARIABLE APSIS_SEPARATION_RING_PREPARATION_ERROR)
if (NOT APSIS_SEPARATION_RING_PREPARATION_RESULT EQUAL 0)
  message(FATAL_ERROR "Separation-ring material preparation failed: ${APSIS_SEPARATION_RING_PREPARATION_ERROR}")
endif ()

add_library(apsis_drift_core STATIC
  "${APSIS_MATERIAL_PREPARED}"
  "${APSIS_CHECKPOINT_MATERIAL_PREPARED}"
  "${APSIS_HATCH_SEAL_PREPARED}"
  "${APSIS_SEPARATION_RING_PREPARED}"
  src/celestial.cpp
  src/coordinates.cpp
  src/craft_frame.cpp
  src/freedom_save.cpp
  src/freedom_flight_save.cpp
  src/freedom_docking_save.cpp
  src/freedom_journey_save.cpp
  src/freedom_starting_assembly_save.cpp
  src/native_starting_assembly.cpp
  src/origin_walker.cpp
  src/operating_motion.cpp
  src/origin_boarding_support.cpp
  src/origin_cabin_seam.cpp
  src/origin_lower_cockpit_contact.cpp
  src/origin_stowed_contact_partition.cpp
  src/origin_cabin_corridor.cpp
  src/origin_boarding_body.cpp
  src/origin_boarding_route_body.cpp
  src/origin_boarding_route_foot_phase.cpp
  src/origin_boarding_initial_material.cpp
  src/origin_boarding_checkpoint_material_extension.cpp
  src/origin_boarding_hatch_seal_material.cpp
  src/origin_boarding_separation_ring_material.cpp
  src/origin_boarding_route_port_unload.cpp
  src/origin_boarding_route_checkpoint_unload.cpp
  src/origin_boarding_route_checkpoint_unload_self02.cpp
  src/origin_boarding_route_checkpoint_world_material.cpp
  src/origin_boarding_boot_support.cpp
  src/origin_boarding_world_checkpoint.cpp
  src/origin_boarding_source_endpoint_surface_checkpoint.cpp
  src/origin_boarding_lower_foot_transfer.cpp
  src/origin_boarding_lower_foot_transfer_surface_sweep.cpp
  src/origin_boarding_planted_legs.cpp
  src/origin_boarding_self_model02.cpp
  src/origin_boarding_self_model03.cpp
  src/rigid_body.cpp
  src/rigid_frame_handoff.cpp
  src/planet_rotation.cpp
  src/physical_local_system.cpp
  src/orbital_telemetry.cpp
  src/orbit_hold.cpp
  src/vacuum_dynamics.cpp
  src/touchdown.cpp
  src/intersystem_contract.cpp
  src/intersystem_jump.cpp
  src/intersystem_planetfall.cpp
  src/local_system.cpp
  src/mission_board.cpp
  src/native_startup.cpp
  src/native_flight_session.cpp
  src/onboarding.cpp
  src/origin_station.cpp
  src/station_geometry.cpp
  src/origin_docking.cpp
  src/origin_return.cpp
  src/origin_system_contract.cpp
  src/planet.cpp
  src/planetary_flight.cpp
  src/profile_catalog.cpp
  src/render_profile.cpp
  src/save_file.cpp
  src/save_schema.cpp
  src/seed.cpp
  src/signal_collection.cpp
  src/signal_run.cpp
  src/signal_scanner.cpp
  src/simulation_clock.cpp
  src/surface_signals.cpp
  src/system_flight.cpp
  src/terrain_tiles.cpp
  src/universe_navigation.cpp
  src/universe_navigation_acceptance.cpp
  src/world_delta_journal.cpp
)
add_library(apsis-drift::core ALIAS apsis_drift_core)

target_include_directories(apsis_drift_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_features(apsis_drift_core PUBLIC cxx_std_23)
target_link_libraries(apsis_drift_core PRIVATE nlohmann_json::nlohmann_json)

# Native state v1 specifies rounded binary64 products/sums, not fused multiply
# adds. Keep this local: legacy replay math has its own compatibility contract.
set_property(SOURCE src/rigid_body.cpp src/rigid_frame_handoff.cpp src/planet_rotation.cpp src/physical_local_system.cpp src/vacuum_dynamics.cpp src/touchdown.cpp src/origin_walker.cpp src/operating_motion.cpp src/origin_boarding_support.cpp src/origin_cabin_seam.cpp src/origin_lower_cockpit_contact.cpp src/origin_cabin_corridor.cpp src/origin_boarding_body.cpp src/origin_boarding_boot_support.cpp src/origin_boarding_world_checkpoint.cpp src/origin_boarding_source_endpoint_surface_checkpoint.cpp src/origin_boarding_lower_foot_transfer.cpp src/origin_boarding_lower_foot_transfer_surface_sweep.cpp src/origin_boarding_planted_legs.cpp src/origin_boarding_self_model02.cpp src/origin_boarding_self_model03.cpp APPEND PROPERTY COMPILE_OPTIONS
  $<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>)
set_property(SOURCE src/origin_boarding_route_body.cpp src/origin_boarding_route_foot_phase.cpp src/origin_boarding_initial_material.cpp src/origin_boarding_checkpoint_material_extension.cpp src/origin_boarding_hatch_seal_material.cpp src/origin_boarding_separation_ring_material.cpp src/origin_boarding_route_port_unload.cpp src/origin_boarding_route_checkpoint_unload.cpp src/origin_boarding_route_checkpoint_unload_self02.cpp src/origin_boarding_route_checkpoint_world_material.cpp APPEND PROPERTY COMPILE_OPTIONS
  $<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>)

if (APSIS_DRIFT_TERMINAL)
add_library(apsis_drift_lib STATIC
  src/audio.cpp
  src/audio_callback.cpp
  src/audio_pack.cpp
  src/benchmark.cpp
  src/cockpit.cpp
  src/flight_deck_acceptance.cpp
  src/flight_input.cpp
  src/intersystem_contract_acceptance.cpp
  src/intersystem_jump_acceptance.cpp
  src/intersystem_jump_render.cpp
  src/intersystem_planetfall_acceptance.cpp
  src/intersystem_return_acceptance.cpp
  src/landscape.cpp
  src/menu.cpp
  src/midi_spike.cpp
  src/onboarding_acceptance.cpp
  src/orbital.cpp
  src/origin_system_contract_acceptance.cpp
  src/planetfall_acceptance.cpp
  src/planetary_presentation.cpp
  src/signal_navigation_acceptance.cpp
  src/signal_run_acceptance.cpp
  src/signal_input.cpp
  src/simulation.cpp
  src/system_flight_acceptance.cpp
  src/system_rendering.cpp
  src/title.cpp
)
add_library(apsis-drift::lib ALIAS apsis_drift_lib)

target_include_directories(apsis_drift_lib
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${tinysoundfont_SOURCE_DIR}"
)
target_compile_features(apsis_drift_lib PUBLIC cxx_std_23)
target_link_libraries(apsis_drift_lib
  PUBLIC apsis-drift::core termforge::lib
  PRIVATE nlohmann_json::nlohmann_json
)
if (APSIS_DRIFT_RTAUDIO)
  target_sources(apsis_drift_lib PRIVATE src/rtaudio_backend.cpp)
  target_compile_definitions(apsis_drift_lib
    PRIVATE APSIS_DRIFT_HAS_RTAUDIO=1)
  target_link_libraries(apsis_drift_lib PRIVATE rtaudio)
endif ()

add_executable(apsis-drift
  src/main.cpp
)
target_link_libraries(apsis-drift PRIVATE apsis-drift::lib)
endif ()

if (APSIS_DRIFT_GODOT_SPIKE)
  if (APSIS_DRIFT_GODOT_LIVE)
    set_target_properties(apsis_drift_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
  endif ()
  add_subdirectory(src/godot)
endif ()

add_library(apsis_drift_asset_manifest STATIC
  src/asset_manifest.cpp
)
target_include_directories(apsis_drift_asset_manifest
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(apsis_drift_asset_manifest PUBLIC cxx_std_23)
target_link_libraries(apsis_drift_asset_manifest
  PRIVATE nlohmann_json::nlohmann_json)

add_executable(apsis-drift-asset-validator
  src/asset_manifest_main.cpp
)
target_link_libraries(apsis-drift-asset-validator
  PRIVATE apsis_drift_asset_manifest)

if (APSIS_DRIFT_TERMINAL AND APSIS_DRIFT_MIDI_SPIKE)
  add_executable(apsis-drift-midi-spike
    src/midi_spike_main.cpp
  )
  target_include_directories(apsis-drift-midi-spike PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(apsis-drift-midi-spike
    PRIVATE apsis-drift::lib)
  target_compile_options(apsis-drift-midi-spike PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_executable(apsis-drift-audio-pack-audition
    src/audio_pack_audition_main.cpp
  )
  target_link_libraries(apsis-drift-audio-pack-audition PRIVATE
    apsis-drift::lib)
  target_compile_options(apsis-drift-audio-pack-audition PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
endif ()

if (APSIS_DRIFT_TESTS AND BUILD_TESTING)
  add_executable(apsis-drift-boarding-separation-ring-material-tests test/origin_boarding_separation_ring_material_test.cpp)
  target_link_libraries(apsis-drift-boarding-separation-ring-material-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-separation-ring-material-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-separation-ring-material-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-separation-ring-material-contract COMMAND apsis-drift-boarding-separation-ring-material-tests)
  set_tests_properties(boarding-separation-ring-material-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-route-checkpoint-world-material04-tests test/origin_boarding_route_checkpoint_world_material04_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-checkpoint-world-material04-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-route-checkpoint-world-material04-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-route-checkpoint-world-material04-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-checkpoint-world-material04-contract COMMAND apsis-drift-boarding-route-checkpoint-world-material04-tests)
  set_tests_properties(boarding-route-checkpoint-world-material04-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-hatch-seal-material-tests test/origin_boarding_hatch_seal_material_test.cpp)
  target_link_libraries(apsis-drift-boarding-hatch-seal-material-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-hatch-seal-material-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-hatch-seal-material-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-hatch-seal-material-contract COMMAND apsis-drift-boarding-hatch-seal-material-tests)
  set_tests_properties(boarding-hatch-seal-material-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-route-checkpoint-world-material03-tests test/origin_boarding_route_checkpoint_world_material03_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-checkpoint-world-material03-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-route-checkpoint-world-material03-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-route-checkpoint-world-material03-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-checkpoint-world-material03-contract COMMAND apsis-drift-boarding-route-checkpoint-world-material03-tests)
  set_tests_properties(boarding-route-checkpoint-world-material03-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-checkpoint-material-extension-tests test/origin_boarding_checkpoint_material_extension_test.cpp)
  target_link_libraries(apsis-drift-boarding-checkpoint-material-extension-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-checkpoint-material-extension-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-checkpoint-material-extension-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-checkpoint-material-extension-contract COMMAND apsis-drift-boarding-checkpoint-material-extension-tests)
  set_tests_properties(boarding-checkpoint-material-extension-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-route-checkpoint-world-material02-tests test/origin_boarding_route_checkpoint_world_material02_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-checkpoint-world-material02-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-route-checkpoint-world-material02-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-route-checkpoint-world-material02-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-checkpoint-world-material02-contract COMMAND apsis-drift-boarding-route-checkpoint-world-material02-tests)
  set_tests_properties(boarding-route-checkpoint-world-material02-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-route-checkpoint-world-material-tests test/origin_boarding_route_checkpoint_world_material_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-checkpoint-world-material-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-route-checkpoint-world-material-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-route-checkpoint-world-material-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-checkpoint-world-material-contract COMMAND apsis-drift-boarding-route-checkpoint-world-material-tests)
  set_tests_properties(boarding-route-checkpoint-world-material-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-route-checkpoint-unload-self02-tests test/origin_boarding_route_checkpoint_unload_self02_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-checkpoint-unload-self02-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-route-checkpoint-unload-self02-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-route-checkpoint-unload-self02-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-checkpoint-unload-self02-contract COMMAND apsis-drift-boarding-route-checkpoint-unload-self02-tests)
  set_tests_properties(boarding-route-checkpoint-unload-self02-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-route-checkpoint-unload-tests test/origin_boarding_route_checkpoint_unload_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-checkpoint-unload-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-route-checkpoint-unload-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-route-checkpoint-unload-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-checkpoint-unload-contract COMMAND apsis-drift-boarding-route-checkpoint-unload-tests)
  set_tests_properties(boarding-route-checkpoint-unload-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-route-port-unload-tests test/origin_boarding_route_port_unload_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-port-unload-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-route-port-unload-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-route-port-unload-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-port-unload-contract COMMAND apsis-drift-boarding-route-port-unload-tests)
  set_tests_properties(boarding-route-port-unload-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-initial-material-tests test/origin_boarding_initial_material_test.cpp)
  target_link_libraries(apsis-drift-boarding-initial-material-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-initial-material-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-initial-material-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-initial-material-contract COMMAND apsis-drift-boarding-initial-material-tests)
  set_tests_properties(boarding-initial-material-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-route-foot-phase-tests test/origin_boarding_route_foot_phase_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-foot-phase-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-route-foot-phase-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-route-foot-phase-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-foot-phase-contract COMMAND apsis-drift-boarding-route-foot-phase-tests)
  set_tests_properties(boarding-route-foot-phase-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-route-body-tests test/origin_boarding_route_body_test.cpp)
  target_link_libraries(apsis-drift-boarding-route-body-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-boarding-route-body-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-route-body-contract COMMAND apsis-drift-boarding-route-body-tests)
  set_tests_properties(boarding-route-body-contract PROPERTIES TIMEOUT 90)

  add_executable(apsis-drift-boarding-self-model03-tests test/origin_boarding_self_model03_test.cpp)
  target_link_libraries(apsis-drift-boarding-self-model03-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-self-model03-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-self-model03-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-self-model03-contract COMMAND apsis-drift-boarding-self-model03-tests)
  set_tests_properties(boarding-self-model03-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-boarding-self-model02-tests test/origin_boarding_self_model02_test.cpp)
  target_link_libraries(apsis-drift-boarding-self-model02-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-self-model02-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-self-model02-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-self-model02-contract COMMAND apsis-drift-boarding-self-model02-tests)
  set_tests_properties(boarding-self-model02-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-boarding-body-tests test/origin_boarding_body_test.cpp)
  target_link_libraries(apsis-drift-boarding-body-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-body-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-body-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-body-contract COMMAND apsis-drift-boarding-body-tests)
  set_tests_properties(boarding-body-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-boarding-lateral-body02-tests test/origin_boarding_lateral_body02_test.cpp)
  target_link_libraries(apsis-drift-boarding-lateral-body02-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-boarding-lateral-body02-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-lateral-body02-contract COMMAND apsis-drift-boarding-lateral-body02-tests)
  set_tests_properties(boarding-lateral-body02-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-boarding-self-model04-tests test/origin_boarding_self_model04_test.cpp)
  target_link_libraries(apsis-drift-boarding-self-model04-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-boarding-self-model04-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-self-model04-contract COMMAND apsis-drift-boarding-self-model04-tests)
  set_tests_properties(boarding-self-model04-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-boarding-boot-support-tests test/origin_boarding_boot_support_test.cpp)
  target_link_libraries(apsis-drift-boarding-boot-support-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-boarding-boot-support-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-boot-support-contract COMMAND apsis-drift-boarding-boot-support-tests)
  set_tests_properties(boarding-boot-support-contract PROPERTIES TIMEOUT 120)
  add_executable(apsis-drift-boarding-foot-sites-tests test/origin_boarding_foot_sites_test.cpp)
  target_link_libraries(apsis-drift-boarding-foot-sites-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-foot-sites-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-foot-sites-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-foot-sites-contract COMMAND apsis-drift-boarding-foot-sites-tests)
  set_tests_properties(boarding-foot-sites-contract PROPERTIES TIMEOUT 120)
  add_executable(apsis-drift-boarding-source-endpoint-tests test/origin_boarding_source_endpoint_test.cpp)
  target_link_libraries(apsis-drift-boarding-source-endpoint-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-source-endpoint-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-source-endpoint-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-source-endpoint-contract COMMAND apsis-drift-boarding-source-endpoint-tests)
  set_tests_properties(boarding-source-endpoint-contract PROPERTIES TIMEOUT 120)
  add_executable(apsis-drift-boarding-source-endpoint-self-tests test/origin_boarding_source_endpoint_self_test.cpp)
  target_link_libraries(apsis-drift-boarding-source-endpoint-self-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-source-endpoint-self-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-source-endpoint-self-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-source-endpoint-self-contract COMMAND apsis-drift-boarding-source-endpoint-self-tests)
  set_tests_properties(boarding-source-endpoint-self-contract PROPERTIES TIMEOUT 120)
  add_executable(apsis-drift-boarding-source-endpoint-load-tests test/origin_boarding_source_endpoint_load_test.cpp)
  target_link_libraries(apsis-drift-boarding-source-endpoint-load-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-source-endpoint-load-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-source-endpoint-load-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-source-endpoint-load-contract COMMAND apsis-drift-boarding-source-endpoint-load-tests)
  set_tests_properties(boarding-source-endpoint-load-contract PROPERTIES TIMEOUT 120)
  add_executable(apsis-drift-boarding-source-endpoint-surface-checkpoint-tests test/origin_boarding_source_endpoint_surface_checkpoint_test.cpp)
  target_link_libraries(apsis-drift-boarding-source-endpoint-surface-checkpoint-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-source-endpoint-surface-checkpoint-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-source-endpoint-surface-checkpoint-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-source-endpoint-surface-checkpoint-contract COMMAND apsis-drift-boarding-source-endpoint-surface-checkpoint-tests)
  set_tests_properties(boarding-source-endpoint-surface-checkpoint-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-boarding-lower-foot-transfer-tests test/origin_boarding_lower_foot_transfer_test.cpp)
  target_link_libraries(apsis-drift-boarding-lower-foot-transfer-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-lower-foot-transfer-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-lower-foot-transfer-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-lower-foot-transfer-contract COMMAND apsis-drift-boarding-lower-foot-transfer-tests)
  set_tests_properties(boarding-lower-foot-transfer-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-boarding-lower-foot-transfer-surface-sweep-tests test/origin_boarding_lower_foot_transfer_surface_sweep_test.cpp)
  target_link_libraries(apsis-drift-boarding-lower-foot-transfer-surface-sweep-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-lower-foot-transfer-surface-sweep-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-lower-foot-transfer-surface-sweep-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-lower-foot-transfer-surface-sweep-contract COMMAND apsis-drift-boarding-lower-foot-transfer-surface-sweep-tests)
  set_tests_properties(boarding-lower-foot-transfer-surface-sweep-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-boarding-world-checkpoint-tests test/origin_boarding_world_checkpoint_test.cpp)
  target_link_libraries(apsis-drift-boarding-world-checkpoint-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-world-checkpoint-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-world-checkpoint-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-world-checkpoint-contract COMMAND apsis-drift-boarding-world-checkpoint-tests)
  set_tests_properties(boarding-world-checkpoint-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-boarding-planted-legs-tests test/origin_boarding_planted_legs_test.cpp)
  target_link_libraries(apsis-drift-boarding-planted-legs-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-planted-legs-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-planted-legs-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-planted-legs-contract COMMAND apsis-drift-boarding-planted-legs-tests)
  set_tests_properties(boarding-planted-legs-contract PROPERTIES TIMEOUT 120)
  add_executable(apsis-drift-boarding-planted-leg-timing-tests test/origin_boarding_planted_leg_timing_test.cpp)
  target_link_libraries(apsis-drift-boarding-planted-leg-timing-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-planted-leg-timing-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-planted-leg-timing-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-planted-leg-timing-contract COMMAND apsis-drift-boarding-planted-leg-timing-tests)
  set_tests_properties(boarding-planted-leg-timing-contract PROPERTIES TIMEOUT 120)
  add_executable(apsis-drift-boarding-planted-body-tests test/origin_boarding_planted_body_test.cpp)
  target_link_libraries(apsis-drift-boarding-planted-body-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-planted-body-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-planted-body-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-planted-body-contract COMMAND apsis-drift-boarding-planted-body-tests)
  set_tests_properties(boarding-planted-body-contract PROPERTIES TIMEOUT 120)
  add_executable(apsis-drift-boarding-planted-hip-preflight-tests test/origin_boarding_planted_hip_preflight_test.cpp)
  target_link_libraries(apsis-drift-boarding-planted-hip-preflight-tests PRIVATE apsis-drift::core)
  target_include_directories(apsis-drift-boarding-planted-hip-preflight-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(apsis-drift-boarding-planted-hip-preflight-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-planted-hip-preflight-contract COMMAND apsis-drift-boarding-planted-hip-preflight-tests)
  set_tests_properties(boarding-planted-hip-preflight-contract PROPERTIES TIMEOUT 120)
  add_executable(apsis-drift-cabin-corridor-tests test/origin_cabin_corridor_test.cpp)
  target_link_libraries(apsis-drift-cabin-corridor-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_definitions(apsis-drift-cabin-corridor-tests PRIVATE
    APSIS_LOWER_CONTACT_FIXTURE="${APSIS_LOWER_CONTACT_DATA}"
    APSIS_LOWER_CONTACT_POLICY_FIXTURE="${APSIS_LOWER_CONTACT_POLICY}"
    APSIS_CABIN_CONTACT_FIXTURE="${APSIS_CABIN_CONTACT_DATA}")
  target_compile_options(apsis-drift-cabin-corridor-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME cabin-corridor-contract COMMAND apsis-drift-cabin-corridor-tests)
  set_tests_properties(cabin-corridor-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-lower-cockpit-contact-tests test/origin_lower_cockpit_contact_test.cpp)
  target_link_libraries(apsis-drift-lower-cockpit-contact-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_definitions(apsis-drift-lower-cockpit-contact-tests PRIVATE
    APSIS_LOWER_CONTACT_FIXTURE="${APSIS_LOWER_CONTACT_DATA}"
    APSIS_LOWER_CONTACT_POLICY_FIXTURE="${APSIS_LOWER_CONTACT_POLICY}"
    APSIS_CABIN_CONTACT_FIXTURE="${APSIS_CABIN_CONTACT_DATA}")
  target_compile_options(apsis-drift-lower-cockpit-contact-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME lower-cockpit-contact-contract COMMAND apsis-drift-lower-cockpit-contact-tests)
  set_tests_properties(lower-cockpit-contact-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-stowed-lower-cockpit-contact-tests test/origin_stowed_lower_cockpit_contact_test.cpp)
  target_link_libraries(apsis-drift-stowed-lower-cockpit-contact-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_definitions(apsis-drift-stowed-lower-cockpit-contact-tests PRIVATE
    APSIS_LOWER_CONTACT_FIXTURE="${APSIS_LOWER_CONTACT_DATA}"
    APSIS_LOWER_CONTACT_POLICY_FIXTURE="${APSIS_LOWER_CONTACT_POLICY}"
    APSIS_CABIN_CONTACT_FIXTURE="${APSIS_CABIN_CONTACT_DATA}")
  target_compile_options(apsis-drift-stowed-lower-cockpit-contact-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME stowed-lower-cockpit-contact-contract COMMAND apsis-drift-stowed-lower-cockpit-contact-tests)
  set_tests_properties(stowed-lower-cockpit-contact-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-cabin-seam-tests test/origin_cabin_seam_test.cpp)
  target_link_libraries(apsis-drift-cabin-seam-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_definitions(apsis-drift-cabin-seam-tests PRIVATE
    APSIS_CABIN_CONTACT_FIXTURE="${APSIS_CABIN_CONTACT_DATA}")
  target_compile_options(apsis-drift-cabin-seam-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME cabin-seam-contract COMMAND apsis-drift-cabin-seam-tests)
  set_tests_properties(cabin-seam-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-boarding-support-tests test/origin_boarding_support_test.cpp)
  target_link_libraries(apsis-drift-boarding-support-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-boarding-support-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME boarding-support-contract COMMAND apsis-drift-boarding-support-tests)
  set_tests_properties(boarding-support-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-stowed-contact-partition-tests test/origin_stowed_contact_partition_test.cpp)
  target_link_libraries(apsis-drift-stowed-contact-partition-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-stowed-contact-partition-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME stowed-contact-partition-contract COMMAND apsis-drift-stowed-contact-partition-tests)
  set_tests_properties(stowed-contact-partition-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-operating-motion-tests test/operating_motion_test.cpp)
  target_link_libraries(apsis-drift-operating-motion-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_definitions(apsis-drift-operating-motion-tests PRIVATE
    APSIS_OPERATING_MOTION_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/native/operating-motion-01/sources")
  target_compile_options(apsis-drift-operating-motion-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME operating-motion-contract COMMAND apsis-drift-operating-motion-tests)
  set_tests_properties(operating-motion-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-origin-walker-tests test/origin_walker_test.cpp)
  target_link_libraries(apsis-drift-origin-walker-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-origin-walker-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
  add_test(NAME origin-walker-contract COMMAND apsis-drift-origin-walker-tests)
  set_tests_properties(origin-walker-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-freedom-journey-save-tests test/freedom_journey_save_test.cpp)
  target_link_libraries(apsis-drift-freedom-journey-save-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-freedom-journey-save-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
  add_test(NAME freedom-journey-save-contract COMMAND apsis-drift-freedom-journey-save-tests)
  set_tests_properties(freedom-journey-save-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-starting-assembly-save-tests test/freedom_starting_assembly_save_test.cpp)
  target_link_libraries(apsis-drift-starting-assembly-save-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-starting-assembly-save-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME starting-assembly-save-contract COMMAND apsis-drift-starting-assembly-save-tests)
  set_tests_properties(starting-assembly-save-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-freedom-docking-save-tests test/freedom_docking_save_test.cpp)
  target_link_libraries(apsis-drift-freedom-docking-save-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-freedom-docking-save-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
  add_test(NAME freedom-docking-save-contract COMMAND apsis-drift-freedom-docking-save-tests)
  set_tests_properties(freedom-docking-save-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-native-startup-tests test/native_startup_test.cpp)
  target_link_libraries(apsis-drift-native-startup-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-native-startup-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME native-startup-contract COMMAND apsis-drift-native-startup-tests)
  add_executable(apsis-drift-freedom-flight-save-tests test/freedom_flight_save_test.cpp)
  target_link_libraries(apsis-drift-freedom-flight-save-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-freedom-flight-save-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
  add_test(NAME freedom-flight-save-contract COMMAND apsis-drift-freedom-flight-save-tests)
  set_tests_properties(freedom-flight-save-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-native-flight-session-tests test/native_flight_session_test.cpp)
  target_link_libraries(apsis-drift-native-flight-session-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-native-flight-session-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
  add_test(NAME native-flight-session-contract COMMAND apsis-drift-native-flight-session-tests)
  set_tests_properties(native-flight-session-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-system-flight-validation-tests test/system_flight_validation_test.cpp)
  target_link_libraries(apsis-drift-system-flight-validation-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-system-flight-validation-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
  add_test(NAME system-flight-validation-contract COMMAND apsis-drift-system-flight-validation-tests)
  set_tests_properties(system-flight-validation-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-station-geometry-tests test/station_geometry_test.cpp)
  target_link_libraries(apsis-drift-station-geometry-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-station-geometry-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
  add_test(NAME station-geometry-contract COMMAND apsis-drift-station-geometry-tests)
  add_executable(apsis-drift-craft-frame-tests test/craft_frame_test.cpp)
  target_link_libraries(apsis-drift-craft-frame-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-craft-frame-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME craft-frame-contract COMMAND apsis-drift-craft-frame-tests)
  add_executable(apsis-drift-wayfarer-frame-tests test/wayfarer_frame_test.cpp)
  target_link_libraries(apsis-drift-wayfarer-frame-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-wayfarer-frame-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>)
  add_test(NAME wayfarer-frame-contract COMMAND apsis-drift-wayfarer-frame-tests)
  add_executable(apsis-drift-origin-docking-tests test/origin_docking_test.cpp)
  target_link_libraries(apsis-drift-origin-docking-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-origin-docking-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>)
  add_test(NAME origin-docking-contract COMMAND apsis-drift-origin-docking-tests)
  add_executable(apsis-drift-rigid-body-tests test/rigid_body_test.cpp)
  target_link_libraries(apsis-drift-rigid-body-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-rigid-body-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME rigid-body-contract COMMAND apsis-drift-rigid-body-tests)
  add_executable(apsis-drift-rigid-frame-handoff-tests test/rigid_frame_handoff_test.cpp)
  target_link_libraries(apsis-drift-rigid-frame-handoff-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-rigid-frame-handoff-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME rigid-frame-handoff-contract COMMAND apsis-drift-rigid-frame-handoff-tests)
  set_tests_properties(rigid-frame-handoff-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-physical-rigid-tests test/physical_rigid_test.cpp)
  target_link_libraries(apsis-drift-physical-rigid-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-physical-rigid-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME physical-rigid-contract COMMAND apsis-drift-physical-rigid-tests)
  set_tests_properties(physical-rigid-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-planet-relative-tests test/planet_relative_test.cpp)
  target_link_libraries(apsis-drift-planet-relative-tests PRIVATE apsis-drift::core nlohmann_json::nlohmann_json)
  target_compile_options(apsis-drift-planet-relative-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME planet-relative-contract COMMAND apsis-drift-planet-relative-tests)
  set_tests_properties(planet-relative-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-planet-rotation-tests test/planet_rotation_test.cpp)
  target_link_libraries(apsis-drift-planet-rotation-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-planet-rotation-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME planet-rotation-contract COMMAND apsis-drift-planet-rotation-tests)
  set_tests_properties(planet-rotation-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-physical-local-system-tests test/physical_local_system_test.cpp)
  target_link_libraries(apsis-drift-physical-local-system-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-physical-local-system-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME physical-local-system-contract COMMAND apsis-drift-physical-local-system-tests)
  set_tests_properties(physical-local-system-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-vacuum-dynamics-tests test/vacuum_dynamics_test.cpp)
  target_link_libraries(apsis-drift-vacuum-dynamics-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-vacuum-dynamics-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME vacuum-dynamics-contract COMMAND apsis-drift-vacuum-dynamics-tests)
  set_tests_properties(vacuum-dynamics-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-coasting-vacuum-tests test/coasting_vacuum_test.cpp)
  target_link_libraries(apsis-drift-coasting-vacuum-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-coasting-vacuum-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME coasting-vacuum-contract COMMAND apsis-drift-coasting-vacuum-tests)
  set_tests_properties(coasting-vacuum-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-central-body-tests test/central_body_dynamics_test.cpp)
  target_link_libraries(apsis-drift-central-body-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-central-body-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME central-body-dynamics-contract COMMAND apsis-drift-central-body-tests)
  set_tests_properties(central-body-dynamics-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-orbital-telemetry-tests test/orbital_telemetry_test.cpp)
  target_link_libraries(apsis-drift-orbital-telemetry-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-orbital-telemetry-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME orbital-telemetry-contract COMMAND apsis-drift-orbital-telemetry-tests)
  set_tests_properties(orbital-telemetry-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-orbit-hold-tests test/orbit_hold_test.cpp)
  target_link_libraries(apsis-drift-orbit-hold-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-orbit-hold-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-ffp-contract=off>)
  add_test(NAME orbit-hold-contract COMMAND apsis-drift-orbit-hold-tests)
  set_tests_properties(orbit-hold-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-atmospheric-flight-tests test/atmospheric_flight_test.cpp)
  target_link_libraries(apsis-drift-atmospheric-flight-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-atmospheric-flight-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
  add_test(NAME atmospheric-flight-contract COMMAND apsis-drift-atmospheric-flight-tests)
  set_tests_properties(atmospheric-flight-contract PROPERTIES TIMEOUT 180)
  add_executable(apsis-drift-touchdown-tests test/touchdown_test.cpp)
  target_link_libraries(apsis-drift-touchdown-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-touchdown-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME touchdown-envelope-contract COMMAND apsis-drift-touchdown-tests)
  set_tests_properties(touchdown-envelope-contract PROPERTIES TIMEOUT 90)
  add_executable(apsis-drift-rotation-coast-tests
    test/godot/rotation_coast_test.cpp)
  target_link_libraries(apsis-drift-rotation-coast-tests PRIVATE apsis-drift::core)
  target_compile_options(apsis-drift-rotation-coast-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME godot-rotation-coast-contract COMMAND apsis-drift-rotation-coast-tests)
  set_tests_properties(godot-rotation-coast-contract PROPERTIES TIMEOUT 90)
endif ()

if (APSIS_DRIFT_TESTS AND BUILD_TESTING)
  add_executable(apsis-drift-asset-manifest-tests
    test/asset_manifest_test.cpp)
  target_link_libraries(apsis-drift-asset-manifest-tests
    PRIVATE apsis_drift_asset_manifest nlohmann_json::nlohmann_json)
  add_test(NAME asset-manifest-contract
    COMMAND apsis-drift-asset-manifest-tests)
  add_test(NAME asset-provenance-manifest
    COMMAND apsis-drift-asset-validator
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/provenance.json")
endif ()

if (APSIS_DRIFT_TESTS AND BUILD_TESTING AND APSIS_DRIFT_TERMINAL)
  add_executable(apsis-drift-tests test/test.cpp)
  target_link_libraries(apsis-drift-tests PRIVATE apsis-drift::lib)
  target_include_directories(apsis-drift-tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_definitions(apsis-drift-tests PRIVATE
    APSIS_DRIFT_TEST_RTAUDIO_ENABLED=$<BOOL:${APSIS_DRIFT_RTAUDIO}>)
  add_test(NAME apsis-drift-tests COMMAND apsis-drift-tests)
  add_executable(apsis-drift-midi-spike-tests
    test/midi_spike_test.cpp)
  target_include_directories(apsis-drift-midi-spike-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_definitions(apsis-drift-midi-spike-tests PRIVATE
    "APSIS_DRIFT_MIDI_ASSET_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/assets/music\"")
  target_link_libraries(apsis-drift-midi-spike-tests
    PRIVATE apsis-drift::lib)
  target_compile_options(apsis-drift-midi-spike-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME midi-score-spike-contract
    COMMAND apsis-drift-midi-spike-tests)
  add_executable(apsis-drift-audio-pack-tests
    test/audio_pack_test.cpp)
  target_compile_definitions(apsis-drift-audio-pack-tests PRIVATE
    "APSIS_DRIFT_AUDIO_ASSET_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\"")
  target_link_libraries(apsis-drift-audio-pack-tests
    PRIVATE apsis-drift::lib)
  target_compile_options(apsis-drift-audio-pack-tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
  add_test(NAME first-light-audio-pack-contract
    COMMAND apsis-drift-audio-pack-tests)
  if (APSIS_DRIFT_MIDI_SPIKE)
    add_test(NAME midi-score-spike-offline
      COMMAND apsis-drift-midi-spike
        --midi "${CMAKE_CURRENT_SOURCE_DIR}/assets/music/issue230-layer-demo.mid"
        --soundfont "${CMAKE_CURRENT_SOURCE_DIR}/assets/music/issue230-tonal-prototype.sf2"
        --output "${CMAKE_CURRENT_BINARY_DIR}/issue230-audition.wav"
        --report "${CMAKE_CURRENT_BINARY_DIR}/issue230-report.json"
        --blocks 2000)
    find_program(APSIS_DRIFT_FFMPEG_EXECUTABLE ffmpeg REQUIRED)
    add_test(NAME first-light-audio-level-contract
      COMMAND "${CMAKE_COMMAND}"
        "-DAUDITION_BIN=$<TARGET_FILE:apsis-drift-audio-pack-audition>"
        "-DFFMPEG_BIN=${APSIS_DRIFT_FFMPEG_EXECUTABLE}"
        "-DASSET_DIR=${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_first_light_audio.cmake")
  endif ()
  add_test(NAME version-report COMMAND apsis-drift --version)
  set_tests_properties(version-report PROPERTIES
    PASS_REGULAR_EXPRESSION "apsis-drift ${PROJECT_VERSION}")

  add_test(NAME benchmark-profile-remote
    COMMAND apsis-drift --benchmark 1 --profile remote)
  add_test(NAME benchmark-profile-balanced
    COMMAND apsis-drift --benchmark 1 --profile balanced)
  add_test(NAME benchmark-viewport-640x360
    COMMAND apsis-drift --benchmark 1 --viewport 640x360)
  add_test(NAME benchmark-profile-local
    COMMAND apsis-drift --benchmark 1 --profile local)
  add_test(NAME benchmark-orbital-remote
    COMMAND apsis-drift --benchmark 1 --profile remote --workload orbital)
  add_test(NAME benchmark-orbital-local
    COMMAND apsis-drift --benchmark 1 --profile local --workload orbital)
  add_test(NAME benchmark-system-remote
    COMMAND apsis-drift --benchmark 1 --profile remote --workload system)
  add_test(NAME benchmark-system-local
    COMMAND apsis-drift --benchmark 1 --profile local --workload system)
  add_test(NAME benchmark-procedural-flight-audio
    COMMAND apsis-drift --audio-benchmark 12)

  add_test(NAME system-navigation-acceptance-matrix
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_system_navigation_acceptance.cmake")
  add_test(NAME intersystem-jump-acceptance-matrix
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_intersystem_jump_acceptance.cmake")
  add_test(NAME system-flight-acceptance-matrix
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_system_flight_acceptance.cmake")
  add_test(NAME intersystem-planetfall-acceptance-matrix
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_intersystem_planetfall_acceptance.cmake")
  add_test(NAME intersystem-return-acceptance-matrix
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_intersystem_return_acceptance.cmake")
  add_test(NAME intersystem-contract-acceptance-matrix
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_intersystem_contract_acceptance.cmake")
  add_test(NAME origin-system-contract-acceptance-matrix
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_origin_system_contract_acceptance.cmake")
  add_test(NAME universe-navigation-acceptance
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_universe_navigation_acceptance.cmake")
  add_test(NAME benchmark-planetary-remote
    COMMAND apsis-drift --benchmark 4 --profile remote --workload planetary)
  add_test(NAME benchmark-planetary-local
    COMMAND apsis-drift --benchmark 4 --profile local --workload planetary)
  add_test(NAME benchmark-viewport-800x600
    COMMAND apsis-drift --benchmark 1 --viewport 800x600)
  add_test(NAME benchmark-profile-cinematic
    COMMAND apsis-drift --benchmark 1 --profile cinematic)

  add_test(NAME benchmark-sweep-report
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_sweep.cmake")

  add_test(NAME benchmark-fixed-step-determinism
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_fixed_step_benchmark.cmake")

  add_test(NAME benchmark-headless-driver-matrix
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_headless_driver_benchmark.cmake")

  add_test(NAME flight-deck-acceptance-matrix
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_flight_deck_acceptance.cmake")

  add_test(NAME planetfall-acceptance-matrix
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_planetfall_acceptance.cmake")

  add_test(NAME signal-collection-acceptance-matrix
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_signal_navigation_acceptance.cmake")

  add_test(NAME signal-run-acceptance-matrix
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_signal_run_acceptance.cmake")

  add_test(NAME guided-departure-acceptance-matrix
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_guided_departure_acceptance.cmake")

  add_test(NAME onboarding-acceptance-matrix
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_onboarding_acceptance.cmake")
  set_tests_properties(onboarding-acceptance-matrix
    PROPERTIES TIMEOUT 2700)

  add_test(NAME reject-unsupported-alpha-save
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            "-DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_unsupported_alpha_save.cmake")

  add_test(NAME reject-viewport-zero
    COMMAND apsis-drift --benchmark 1 --viewport 0x480)
  add_test(NAME reject-system-flight-missing-report
    COMMAND apsis-drift --system-flight-acceptance)
  set_tests_properties(reject-system-flight-missing-report
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME reject-system-flight-driver
    COMMAND apsis-drift --system-flight-acceptance --driver kitty
            --report system-flight-invalid.json)
  set_tests_properties(reject-system-flight-driver
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME reject-intersystem-planetfall-missing-report
    COMMAND apsis-drift --intersystem-planetfall-acceptance)
  set_tests_properties(reject-intersystem-planetfall-missing-report
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME reject-intersystem-planetfall-driver
    COMMAND apsis-drift --intersystem-planetfall-acceptance --driver kitty
            --report intersystem-planetfall-invalid.json)
  set_tests_properties(reject-intersystem-planetfall-driver
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME reject-intersystem-planetfall-mode-conflict
    COMMAND apsis-drift --intersystem-planetfall-acceptance --benchmark 1
            --report intersystem-planetfall-invalid.json)
  set_tests_properties(reject-intersystem-planetfall-mode-conflict
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME reject-intersystem-return-missing-report
    COMMAND apsis-drift --intersystem-return-acceptance)
  set_tests_properties(reject-intersystem-return-missing-report
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME reject-intersystem-return-driver
    COMMAND apsis-drift --intersystem-return-acceptance --driver kitty
            --report intersystem-return-invalid.json)
  set_tests_properties(reject-intersystem-return-driver
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME reject-intersystem-return-mode-conflict
    COMMAND apsis-drift --intersystem-return-acceptance --benchmark 1
            --report intersystem-return-invalid.json)
  set_tests_properties(reject-intersystem-return-mode-conflict
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME reject-intersystem-contract-missing-report
    COMMAND apsis-drift --intersystem-contract-acceptance)
  set_tests_properties(reject-intersystem-contract-missing-report
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME reject-intersystem-contract-driver
    COMMAND apsis-drift --intersystem-contract-acceptance --driver kitty
            --report intersystem-contract-invalid.json)
  set_tests_properties(reject-intersystem-contract-driver
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME reject-intersystem-contract-mode-conflict
    COMMAND apsis-drift --intersystem-contract-acceptance --benchmark 1
            --report intersystem-contract-invalid.json)
  set_tests_properties(reject-intersystem-contract-mode-conflict
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME reject-origin-system-contract-missing-report
    COMMAND apsis-drift --origin-system-contract-acceptance)
  set_tests_properties(reject-origin-system-contract-missing-report
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME reject-origin-system-contract-driver
    COMMAND apsis-drift --origin-system-contract-acceptance --driver kitty
            --report origin-system-contract-invalid.json)
  set_tests_properties(reject-origin-system-contract-driver
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME reject-origin-system-contract-mode-conflict
    COMMAND apsis-drift --origin-system-contract-acceptance --benchmark 1
            --report origin-system-contract-invalid.json)
  set_tests_properties(reject-origin-system-contract-mode-conflict
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME reject-universe-navigation-missing-report
    COMMAND apsis-drift --universe-navigation-acceptance)
  set_tests_properties(reject-universe-navigation-missing-report
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME reject-universe-navigation-driver
    COMMAND apsis-drift --universe-navigation-acceptance --driver kitty
            --report universe-navigation-invalid.json)
  set_tests_properties(reject-universe-navigation-driver
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME reject-universe-navigation-mode-conflict
    COMMAND apsis-drift --universe-navigation-acceptance --benchmark 1
            --report universe-navigation-invalid.json)
  set_tests_properties(reject-universe-navigation-mode-conflict
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME reject-viewport-negative
    COMMAND apsis-drift --benchmark 1 --viewport -1x480)
  add_test(NAME reject-viewport-malformed
    COMMAND apsis-drift --benchmark 1 --viewport 640by480)
  add_test(NAME reject-viewport-overflow
    COMMAND apsis-drift --benchmark 1 --viewport
            999999999999999999999999x480)
  add_test(NAME reject-viewport-axis
    COMMAND apsis-drift --benchmark 1 --viewport 4097x1)
  add_test(NAME reject-viewport-pixel-budget
    COMMAND apsis-drift --benchmark 1 --viewport 4096x1025)
  add_test(NAME reject-sweep-zero-frames
    COMMAND apsis-drift --sweep 0 --report sweep-invalid.json)
  add_test(NAME reject-sweep-missing-report
    COMMAND apsis-drift --sweep 1)
  add_test(NAME reject-sweep-invalid-fps
    COMMAND apsis-drift --sweep 1 --sweep-fps 30,0
            --report sweep-invalid.json)
  add_test(NAME reject-sweep-duplicate-viewport
    COMMAND apsis-drift --sweep 1 --sweep-viewports remote,320x240
            --report sweep-invalid.json)
  add_test(NAME reject-sweep-incompatible-profile
    COMMAND apsis-drift --sweep 1 --profile remote
            --report sweep-invalid.json)
  add_test(NAME reject-sweep-options-without-mode
    COMMAND apsis-drift --sweep-fps 30 --report sweep-invalid.json)
  add_test(NAME reject-workload-without-mode
    COMMAND apsis-drift --workload orbital)
  add_test(NAME reject-profile-options-with-benchmark
    COMMAND apsis-drift --benchmark 1 --new-game-seed 0)
  add_test(NAME reject-load-and-new-game
    COMMAND apsis-drift --load profile.json --new-game-seed 1)
  add_test(NAME reject-profile-seed-alias
    COMMAND apsis-drift --save profile.json --seed 1)
  add_test(NAME reject-negative-new-game-seed
    COMMAND apsis-drift --new-game-seed -1)
  add_test(NAME reject-missing-load
    COMMAND apsis-drift --load definitely-missing-save.json)
  add_test(NAME reject-keyboard-with-automatic-driver
    COMMAND apsis-drift --keyboard enhanced)
  add_test(NAME reject-acceptance-missing-report
    COMMAND apsis-drift --flight-deck-acceptance)
  add_test(NAME reject-acceptance-seed-override
    COMMAND apsis-drift --flight-deck-acceptance --seed 42
            --report acceptance-invalid.json)
  add_test(NAME reject-acceptance-mode-conflict
    COMMAND apsis-drift --flight-deck-acceptance --benchmark 1
            --report acceptance-invalid.json)
  add_test(NAME reject-planetfall-missing-report
    COMMAND apsis-drift --planetfall-acceptance)
  add_test(NAME reject-planetfall-seed-override
    COMMAND apsis-drift --planetfall-acceptance --seed 42
            --report planetfall-invalid.json)
  add_test(NAME reject-planetfall-mode-conflict
    COMMAND apsis-drift --planetfall-acceptance --benchmark 1
            --report planetfall-invalid.json)
  add_test(NAME reject-planetfall-driver
    COMMAND apsis-drift --planetfall-acceptance --driver kitty
            --report planetfall-invalid.json)
  add_test(NAME reject-signal-navigation-missing-report
    COMMAND apsis-drift --signal-navigation-acceptance)
  add_test(NAME reject-signal-navigation-seed-override
    COMMAND apsis-drift --signal-navigation-acceptance --seed 42
            --report signal-navigation-invalid.json)
  add_test(NAME reject-signal-navigation-mode-conflict
    COMMAND apsis-drift --signal-navigation-acceptance --benchmark 1
            --report signal-navigation-invalid.json)
  add_test(NAME reject-signal-run-missing-report
    COMMAND apsis-drift --signal-run-acceptance)
  add_test(NAME reject-signal-run-driver
    COMMAND apsis-drift --signal-run-acceptance --driver ansi
            --report signal-run-invalid.json)
  add_test(NAME reject-signal-run-seed-override
    COMMAND apsis-drift --signal-run-acceptance --seed 42
            --report signal-run-invalid.json)
  add_test(NAME reject-signal-run-mode-conflict
    COMMAND apsis-drift --signal-run-acceptance --benchmark 1
            --report signal-run-invalid.json)
  add_test(NAME reject-guided-departure-missing-report
    COMMAND apsis-drift --guided-departure-acceptance --driver ansi)
  add_test(NAME reject-guided-departure-missing-driver
    COMMAND apsis-drift --guided-departure-acceptance
            --report guided-departure-invalid.json)
  add_test(NAME reject-onboarding-missing-report
    COMMAND apsis-drift --onboarding-acceptance --driver ansi)
  add_test(NAME reject-onboarding-missing-driver
    COMMAND apsis-drift --onboarding-acceptance
            --report onboarding-invalid.json)
  add_test(NAME reject-system-navigation-missing-report
    COMMAND apsis-drift --system-navigation-acceptance --driver ansi)
  add_test(NAME reject-system-navigation-missing-driver
    COMMAND apsis-drift --system-navigation-acceptance
            --report system-navigation-invalid.json)
  add_test(NAME reject-system-navigation-mode-conflict
    COMMAND apsis-drift --system-navigation-acceptance --driver kitty
            --benchmark 1 --report system-navigation-invalid.json)
  add_test(NAME reject-intersystem-jump-missing-report
    COMMAND apsis-drift --intersystem-jump-acceptance)
  add_test(NAME reject-intersystem-jump-driver
    COMMAND apsis-drift --intersystem-jump-acceptance --driver ansi
            --report intersystem-jump-invalid.json)
  add_test(NAME reject-intersystem-jump-mode-conflict
    COMMAND apsis-drift --intersystem-jump-acceptance --benchmark 1
            --report intersystem-jump-invalid.json)
  add_test(NAME capability-floor-refusal
    COMMAND "${CMAKE_COMMAND}"
            "-DAPSIS_DRIFT_BIN=$<TARGET_FILE:apsis-drift>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_capability_refusal.cmake")
  set_tests_properties(
    reject-viewport-zero
    reject-viewport-negative
    reject-viewport-malformed
    reject-viewport-overflow
    reject-viewport-axis
    reject-viewport-pixel-budget
    reject-sweep-zero-frames
    reject-sweep-missing-report
    reject-sweep-invalid-fps
    reject-sweep-duplicate-viewport
    reject-sweep-incompatible-profile
    reject-sweep-options-without-mode
    reject-workload-without-mode
    reject-profile-options-with-benchmark
    reject-load-and-new-game
    reject-profile-seed-alias
    reject-negative-new-game-seed
    reject-missing-load
    reject-keyboard-with-automatic-driver
    reject-acceptance-missing-report
    reject-acceptance-seed-override
    reject-acceptance-mode-conflict
    reject-planetfall-missing-report
    reject-planetfall-seed-override
    reject-planetfall-mode-conflict
    reject-planetfall-driver
    reject-signal-navigation-missing-report
    reject-signal-navigation-seed-override
    reject-signal-navigation-mode-conflict
    reject-signal-run-missing-report
    reject-signal-run-driver
    reject-signal-run-seed-override
    reject-signal-run-mode-conflict
    reject-guided-departure-missing-report
    reject-guided-departure-missing-driver
    reject-onboarding-missing-report
    reject-onboarding-missing-driver
    reject-system-navigation-missing-report
    reject-system-navigation-missing-driver
    reject-system-navigation-mode-conflict
    reject-intersystem-jump-missing-report
    reject-intersystem-jump-driver
    reject-intersystem-jump-mode-conflict
    PROPERTIES WILL_FAIL TRUE
  )
endif ()
