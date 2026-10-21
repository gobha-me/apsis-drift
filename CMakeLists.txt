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

include(cmake/NativeData.cmake)

add_library(apsis_drift_core STATIC
  src/celestial.cpp
  src/coordinates.cpp
  src/craft_frame.cpp
  src/freedom_save.cpp
  src/freedom_system_network.cpp
  src/freedom_flight_save.cpp
  src/freedom_moon_flight_save.cpp
  src/freedom_docking_save.cpp
  src/freedom_journey_save.cpp
  src/freedom_starting_assembly_save.cpp
  src/freedom_boarding_save.cpp
  src/native_starting_assembly.cpp
  src/origin_walker.cpp
  src/origin_station_crew.cpp
  src/origin_gameplay_boarding.cpp
  src/operating_motion.cpp
  src/origin_boarding_support.cpp
  src/origin_cabin_seam.cpp
  src/origin_lower_cockpit_contact.cpp
  src/origin_stowed_contact_partition.cpp
  src/rigid_body.cpp
  src/rigid_frame_handoff.cpp
  src/planet_rotation.cpp
  src/planet_ambient.cpp
  src/planet_optics.cpp
  src/planet_surface_appearance.cpp
  src/craft_environment_assessment.cpp
  src/physical_local_system.cpp
  src/body_ephemeris.cpp
  src/secondary_bodies.cpp
  src/physical_moon.cpp
  src/physical_moon_rotation.cpp
  src/physical_minor.cpp
  src/physical_minor_rotation.cpp
  src/moon_terrain.cpp
  src/moon_terrain_impact.cpp
  src/moon_surface_contact.cpp
  src/moon_landed_craft.cpp
  src/moon_surface_walker.cpp
  src/secondary_observations.cpp
  src/orbital_telemetry.cpp
  src/orbit_hold.cpp
  src/vacuum_dynamics.cpp
  src/touchdown.cpp
  src/terrain_touchdown.cpp
  src/terrain_impact.cpp
  src/craft_condition.cpp
  src/freedom_condition_save.cpp
  src/native_impact.cpp
  src/terrain_hull_clearance.cpp
  src/landed_craft.cpp
  src/planet_surface_walker.cpp
  src/freedom_surface_walk_save.cpp
  src/native_surface_maneuver.cpp
  src/native_surface_walk.cpp
  src/native_surface_landmarks.cpp
  src/native_field_samples.cpp
  src/freedom_resources.cpp
  src/freedom_resource_save.cpp
  src/freedom_surface_save.cpp
  src/freedom_moon_walk_save.cpp
  src/freedom_local_jump.cpp
  src/freedom_planet_jump.cpp
  src/freedom_moon_surface_save.cpp
  src/godot/contact_surface.cpp
  src/godot/contact_patch.cpp
  src/godot/saved_contact_geometry.cpp
  src/intersystem_contract.cpp
  src/intersystem_jump.cpp
  src/intersystem_planetfall.cpp
  src/local_system.cpp
  src/mission_board.cpp
  src/native_startup.cpp
  src/native_flight_session.cpp
  src/native_moon_surface.cpp
  src/native_moon_walk.cpp
  src/native_local_jump.cpp
  src/freedom_local_travel_save.cpp
  src/native_moon_flight.cpp
  src/native_minor_flight.cpp
  src/freedom_minor_flight_save.cpp
  src/native_first_jump.cpp
  src/native_planet_jump.cpp
  src/native_recovery.cpp
  src/native_port_approach.cpp
  src/native_port_docking.cpp
  src/native_orbital_survey.cpp
  src/native_port_rendezvous.cpp
  src/onboarding.cpp
  src/origin_station.cpp
  src/station_geometry.cpp
  src/origin_docking.cpp
  src/origin_return.cpp
  src/origin_system_contract.cpp
  src/planet.cpp
  src/planetary_flight.cpp
  src/profile_catalog.cpp
  src/native_profile.cpp
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
  src/terrain_local_detail.cpp
  src/surface_landmarks.cpp
  src/moon_surface_landmarks.cpp
  src/native_moon_landmarks.cpp
  src/universe_navigation.cpp
  src/freedom_navigation.cpp
  src/freedom_knowledge.cpp
  src/freedom_knowledge_save.cpp
  src/freedom_local_observations.cpp
  src/freedom_jump_targeting.cpp
  src/freedom_travel_save.cpp
  src/freedom_composed_travel_save.cpp
  src/freedom_recovery_save.cpp
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

# Deferred anatomical certification remains buildable for its contracts. The
# playable native bridge depends only on the authoritative runtime core above.
add_library(apsis_drift_boarding_research STATIC EXCLUDE_FROM_ALL
  "${APSIS_MATERIAL_PREPARED}"
  "${APSIS_CHECKPOINT_MATERIAL_PREPARED}"
  "${APSIS_HATCH_SEAL_PREPARED}"
  "${APSIS_SEPARATION_RING_PREPARED}"
  src/origin_cabin_corridor.cpp
  src/origin_boarding_body.cpp
  src/origin_boarding_route_body.cpp
  src/origin_boarding_route_foot_phase.cpp
  src/origin_boarding_route_intermediate_step.cpp
  src/origin_boarding_route_intermediate_step02.cpp
  src/origin_boarding_intermediate_pause_support.cpp
  src/origin_boarding_intermediate_pause_envelope.cpp
  src/origin_boarding_intermediate_pause_support02.cpp
  src/origin_boarding_route_intermediate_load01.cpp
  src/origin_boarding_route_intermediate_unloaded01.cpp
  src/origin_boarding_route_intermediate_support01.cpp
  src/origin_boarding_route_intermediate_support_self01.cpp
  src/origin_boarding_route_intermediate_root_z01.cpp
  src/origin_boarding_route_intermediate_hip_diagnostic01.cpp
  src/origin_boarding_route_intermediate_hip_diagnostic02.cpp
  src/origin_boarding_intermediate_endpoint01.cpp
  src/origin_boarding_intermediate_reach_evidence01.cpp
  src/origin_boarding_intermediate_endpoint02.cpp
  src/origin_boarding_intermediate_sector_evidence01.cpp
  src/origin_boarding_intermediate_endpoint03.cpp
  src/origin_boarding_intermediate_sector_evidence02.cpp
  src/origin_boarding_intermediate_endpoint04.cpp
  src/origin_boarding_intermediate_endpoint05.cpp
  src/origin_boarding_intermediate_endpoint06.cpp
  src/origin_boarding_knee_compatibility_diagnostic01.cpp
  src/origin_boarding_hip_joint_plane_method01.cpp
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
)
add_library(apsis-drift::boarding-research ALIAS apsis_drift_boarding_research)
target_include_directories(apsis_drift_boarding_research
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(apsis_drift_boarding_research PUBLIC cxx_std_23)
target_link_libraries(apsis_drift_boarding_research
  PUBLIC apsis-drift::core
  PRIVATE nlohmann_json::nlohmann_json)

# Native state v1 specifies rounded binary64 products/sums, not fused multiply
# adds. Keep this local: legacy replay math has its own compatibility contract.
set_property(SOURCE src/physical_minor.cpp src/physical_minor_rotation.cpp src/native_minor_flight.cpp src/secondary_bodies.cpp src/secondary_observations.cpp src/physical_moon.cpp src/physical_moon_rotation.cpp src/moon_terrain.cpp src/moon_surface_contact.cpp src/moon_landed_craft.cpp src/moon_surface_walker.cpp src/moon_terrain_impact.cpp src/native_moon_surface.cpp src/native_moon_walk.cpp src/freedom_local_jump.cpp src/native_local_jump.cpp src/native_moon_flight.cpp src/terrain_local_detail.cpp src/surface_landmarks.cpp src/moon_surface_landmarks.cpp src/native_moon_landmarks.cpp src/native_surface_landmarks.cpp src/native_field_samples.cpp APPEND PROPERTY COMPILE_OPTIONS
  $<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>)
set_property(SOURCE src/freedom_planet_jump.cpp src/freedom_composed_travel_save.cpp src/native_planet_jump.cpp APPEND PROPERTY COMPILE_OPTIONS
  $<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>)
set_property(SOURCE src/freedom_jump_targeting.cpp APPEND PROPERTY COMPILE_OPTIONS
  $<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>)
set_property(SOURCE src/native_first_jump.cpp APPEND PROPERTY COMPILE_OPTIONS
  $<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>)
set_property(SOURCE src/native_port_rendezvous.cpp src/native_port_docking.cpp src/native_orbital_survey.cpp APPEND PROPERTY COMPILE_OPTIONS
  $<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>)
set_property(SOURCE src/freedom_resources.cpp src/freedom_local_observations.cpp src/rigid_body.cpp src/rigid_frame_handoff.cpp src/planet_rotation.cpp src/physical_local_system.cpp src/vacuum_dynamics.cpp src/touchdown.cpp src/terrain_touchdown.cpp src/terrain_hull_clearance.cpp src/landed_craft.cpp src/native_surface_maneuver.cpp src/godot/contact_surface.cpp src/godot/contact_patch.cpp src/godot/saved_contact_geometry.cpp src/origin_walker.cpp src/operating_motion.cpp src/origin_boarding_support.cpp src/origin_cabin_seam.cpp src/origin_lower_cockpit_contact.cpp src/origin_cabin_corridor.cpp src/origin_boarding_body.cpp src/origin_boarding_boot_support.cpp src/origin_boarding_world_checkpoint.cpp src/origin_boarding_source_endpoint_surface_checkpoint.cpp src/origin_boarding_lower_foot_transfer.cpp src/origin_boarding_lower_foot_transfer_surface_sweep.cpp src/origin_boarding_planted_legs.cpp src/origin_boarding_self_model02.cpp src/origin_boarding_self_model03.cpp APPEND PROPERTY COMPILE_OPTIONS
  $<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>)
set_property(SOURCE src/origin_boarding_route_body.cpp src/origin_boarding_route_foot_phase.cpp src/origin_boarding_route_intermediate_step.cpp src/origin_boarding_route_intermediate_step02.cpp src/origin_boarding_intermediate_pause_support.cpp src/origin_boarding_intermediate_pause_envelope.cpp src/origin_boarding_intermediate_pause_support02.cpp src/origin_boarding_route_intermediate_load01.cpp src/origin_boarding_route_intermediate_unloaded01.cpp src/origin_boarding_route_intermediate_support01.cpp src/origin_boarding_route_intermediate_support_self01.cpp src/origin_boarding_route_intermediate_hip_diagnostic01.cpp src/origin_boarding_initial_material.cpp src/origin_boarding_checkpoint_material_extension.cpp src/origin_boarding_hatch_seal_material.cpp src/origin_boarding_separation_ring_material.cpp src/origin_boarding_route_port_unload.cpp src/origin_boarding_route_checkpoint_unload.cpp src/origin_boarding_route_checkpoint_unload_self02.cpp src/origin_boarding_route_checkpoint_world_material.cpp APPEND PROPERTY COMPILE_OPTIONS
  $<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>)
set_property(SOURCE src/origin_boarding_route_intermediate_hip_diagnostic02.cpp APPEND PROPERTY COMPILE_OPTIONS
  $<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>)
set_property(SOURCE src/origin_boarding_route_intermediate_root_z01.cpp APPEND PROPERTY COMPILE_OPTIONS
  $<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>)
set_property(SOURCE src/origin_gameplay_boarding.cpp APPEND PROPERTY COMPILE_OPTIONS
  $<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>)
set_property(SOURCE src/origin_boarding_intermediate_endpoint01.cpp APPEND PROPERTY COMPILE_OPTIONS
  $<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>)
set_property(SOURCE src/origin_boarding_intermediate_reach_evidence01.cpp APPEND PROPERTY COMPILE_OPTIONS
  $<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>)
set_property(SOURCE src/origin_boarding_intermediate_endpoint02.cpp APPEND PROPERTY COMPILE_OPTIONS
  $<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>)
set_property(SOURCE src/origin_boarding_intermediate_sector_evidence01.cpp APPEND PROPERTY COMPILE_OPTIONS
  $<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>)
set_property(SOURCE src/origin_boarding_intermediate_endpoint03.cpp APPEND PROPERTY COMPILE_OPTIONS
  $<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>)
set_property(SOURCE src/origin_boarding_intermediate_sector_evidence02.cpp APPEND PROPERTY COMPILE_OPTIONS
  $<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>)
set_property(SOURCE src/origin_boarding_intermediate_endpoint04.cpp APPEND PROPERTY COMPILE_OPTIONS
  $<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>)
set_property(SOURCE src/origin_boarding_intermediate_endpoint05.cpp APPEND PROPERTY COMPILE_OPTIONS
  $<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>)
set_property(SOURCE src/origin_boarding_intermediate_endpoint06.cpp APPEND PROPERTY COMPILE_OPTIONS
  $<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>)
set_property(SOURCE src/origin_boarding_knee_compatibility_diagnostic01.cpp APPEND PROPERTY COMPILE_OPTIONS
  $<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>)
set_property(SOURCE src/origin_boarding_hip_joint_plane_method01.cpp APPEND PROPERTY COMPILE_OPTIONS
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

include(cmake/CoreTests.cmake)

include(cmake/AssetTests.cmake)

include(cmake/TerminalTests.cmake)
