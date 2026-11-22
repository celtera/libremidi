if(CMAKE_VERSION VERSION_GREATER 3.30)
    set(CMAKE_FETCHCONTENT_SYSTEM_KEYWORD SYSTEM)
endif()

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2
    GIT_TAG        v3.16.1
    ${CMAKE_FETCHCONTENT_SYSTEM_KEYWORD}
)

FetchContent_MakeAvailable(Catch2)
if(NOT TARGET Catch2::Catch2WithMain)
    message(WARNING "libremidi: Catch2::Catch2WithMain target not found")
    return()
endif()

message(STATUS "libremidi: compiling tests")
if(LIBREMIDI_CI)
    target_compile_definitions(libremidi ${_public} LIBREMIDI_CI)
endif()

add_executable(conversion_test tests/unit/conversion.cpp)
target_link_libraries(conversion_test PRIVATE libremidi Catch2::Catch2WithMain)

add_executable(controller_translation_test tests/unit/controller_translation.cpp)
target_link_libraries(controller_translation_test PRIVATE libremidi Catch2::Catch2WithMain)

add_executable(backend_selection_test tests/unit/backend_selection.cpp)
target_link_libraries(backend_selection_test PRIVATE libremidi Catch2::Catch2WithMain)
if(LIBREMIDI_HEADER_ONLY)
  target_sources(backend_selection_test PRIVATE include/libremidi/libremidi-c.cpp)
endif()

add_executable(error_test tests/unit/error.cpp)
target_link_libraries(error_test PRIVATE libremidi Catch2::Catch2WithMain)

add_executable(midiin_test tests/unit/midi_in.cpp)
target_link_libraries(midiin_test PRIVATE libremidi Catch2::Catch2WithMain)

add_executable(midiout_test tests/unit/midi_out.cpp)
target_link_libraries(midiout_test PRIVATE libremidi Catch2::Catch2WithMain)

add_executable(winmidi_raw_output_test tests/unit/winmidi_raw_output.cpp)
target_link_libraries(winmidi_raw_output_test PRIVATE libremidi Catch2::Catch2WithMain)

add_executable(midifile_read_test tests/unit/midifile_read.cpp)
target_link_libraries(midifile_read_test PRIVATE libremidi Catch2::Catch2WithMain)
target_compile_definitions(midifile_read_test PRIVATE "LIBREMIDI_TEST_CORPUS=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus\"")

add_executable(midifile_write_test tests/unit/midifile_write.cpp)
target_link_libraries(midifile_write_test PRIVATE libremidi Catch2::Catch2WithMain)
target_compile_definitions(midifile_write_test PRIVATE "LIBREMIDI_TEST_CORPUS=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus\"")

add_executable(midifile_write_tracks_test tests/integration/midifile_write_tracks.cpp)
target_link_libraries(midifile_write_tracks_test PRIVATE libremidi Catch2::Catch2WithMain)

add_executable(protocols_test tests/unit/protocols.cpp)
target_link_libraries(protocols_test PRIVATE libremidi Catch2::Catch2WithMain)

add_executable(midi_stream_decoder_test tests/unit/midi_stream_decoder.cpp)
target_link_libraries(midi_stream_decoder_test PRIVATE libremidi Catch2::Catch2WithMain)

add_executable(midi_stream_decoder_stream_test tests/unit/midi_stream_decoder_stream.cpp)
target_link_libraries(midi_stream_decoder_stream_test PRIVATE libremidi Catch2::Catch2WithMain)

add_executable(midi_timing_test tests/unit/midi_timing.cpp)
target_link_libraries(midi_timing_test PRIVATE libremidi Catch2::Catch2WithMain)

add_executable(rawio_test tests/unit/rawio.cpp)
target_link_libraries(rawio_test PRIVATE libremidi Catch2::Catch2WithMain)

add_executable(observer_filters_test tests/unit/observer_filters.cpp)
target_link_libraries(observer_filters_test PRIVATE libremidi Catch2::Catch2WithMain)

include(CTest)
add_test(NAME conversion_test COMMAND conversion_test)
add_test(NAME controller_translation_test COMMAND controller_translation_test)
add_test(NAME error_test COMMAND error_test)
add_test(NAME backend_selection_test COMMAND backend_selection_test)
add_test(NAME midiin_test COMMAND midiin_test --allow-running-no-tests)
add_test(NAME midiout_test COMMAND midiout_test --allow-running-no-tests)
add_test(NAME winmidi_raw_output_test COMMAND winmidi_raw_output_test)
add_test(NAME midifile_read_test COMMAND midifile_read_test)
add_test(NAME midifile_write_test COMMAND midifile_write_test)
add_test(NAME midifile_write_tracks_test COMMAND midifile_write_tracks_test)
add_test(NAME protocols_test COMMAND protocols_test)
add_test(NAME midi_stream_decoder_test COMMAND midi_stream_decoder_test)
add_test(NAME midi_stream_decoder_stream_test COMMAND midi_stream_decoder_stream_test "~[bench]")
add_test(NAME midi_timing_test COMMAND midi_timing_test)
add_test(NAME rawio_test COMMAND rawio_test)
add_test(NAME observer_filters_test COMMAND observer_filters_test)

if(LIBREMIDI_HAS_WINMIDI)
  add_executable(winmidi_activation_test tests/integration/winmidi_activation.cpp)
  target_link_libraries(winmidi_activation_test PRIVATE libremidi)
  add_test(NAME winmidi_activation_test COMMAND winmidi_activation_test)
  add_executable(winmidi_sessions_test tests/integration/winmidi_sessions.cpp)
  target_link_libraries(winmidi_sessions_test PRIVATE libremidi)
  add_test(NAME winmidi_sessions_test COMMAND winmidi_sessions_test)
  set_tests_properties(winmidi_sessions_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
  add_executable(winmidi_replug_test tests/integration/winmidi_replug.cpp)
  target_link_libraries(winmidi_replug_test PRIVATE libremidi)
  add_test(NAME winmidi_replug_test COMMAND winmidi_replug_test)
  set_tests_properties(winmidi_replug_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
  add_executable(winmidi_session_registry_test tests/unit/winmidi_session_registry.cpp)
  target_link_libraries(winmidi_session_registry_test PRIVATE libremidi)
  add_test(NAME winmidi_session_registry_test COMMAND winmidi_session_registry_test)
  # A host unloads a module that embeds libremidi. No MIDI session is created.
  add_library(winmidi_unload_plugin MODULE tests/integration/winmidi_unload_plugin.cpp)
  target_link_libraries(winmidi_unload_plugin PRIVATE libremidi)
  add_executable(winmidi_unload_test tests/integration/winmidi_unload.cpp)
  add_dependencies(winmidi_unload_test winmidi_unload_plugin)
  add_test(NAME winmidi_unload_test
           COMMAND winmidi_unload_test $<TARGET_FILE:winmidi_unload_plugin>)
  set_tests_properties(winmidi_unload_test PROPERTIES TIMEOUT 60)
endif()

add_executable(winmidi_pool_test tests/unit/winmidi_pool.cpp)
target_link_libraries(winmidi_pool_test PRIVATE libremidi)
add_test(NAME winmidi_pool_test COMMAND winmidi_pool_test)
set_tests_properties(winmidi_pool_test PROPERTIES TIMEOUT 60)

add_executable(winmidi_groups_test tests/unit/winmidi_groups.cpp)
target_link_libraries(winmidi_groups_test PRIVATE libremidi)
add_test(NAME winmidi_groups_test COMMAND winmidi_groups_test)

if(LIBREMIDI_HAS_COREMIDI)
  add_executable(coremidi_observer_test tests/integration/coremidi_observer.cpp)
  target_link_libraries(coremidi_observer_test PRIVATE libremidi Catch2::Catch2WithMain)
  add_test(NAME coremidi_observer_test COMMAND coremidi_observer_test)
endif()

# PipeWire shared-context regression tests. Standalone programs (no Catch2):
# each skips with exit 0 when no daemon is reachable and arms a watchdog so a
# lock-corruption regression fails instead of hanging.
if(LIBREMIDI_HAS_PIPEWIRE)
  foreach(_pwtest pipewire_context_sync pipewire_context_reconnect pipewire_context_subscriptions
                  pipewire_context_error_scope pipewire_context_shared_teardown
                  pipewire_midi_port_lifecycle pipewire_context_object_errors)
    add_executable(${_pwtest}_test tests/integration/${_pwtest}.cpp)
    target_link_libraries(${_pwtest}_test PRIVATE libremidi)
    add_test(NAME ${_pwtest}_test COMMAND ${_pwtest}_test)
  endforeach()
endif()
