FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.4.0
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

add_executable(error_test tests/unit/error.cpp)
target_link_libraries(error_test PRIVATE libremidi Catch2::Catch2WithMain)

add_executable(midiin_test tests/unit/midi_in.cpp)
target_link_libraries(midiin_test PRIVATE libremidi Catch2::Catch2WithMain)

add_executable(midiout_test tests/unit/midi_out.cpp)
target_link_libraries(midiout_test PRIVATE libremidi Catch2::Catch2WithMain)

add_executable(midifile_read_test tests/unit/midifile_read.cpp)
target_link_libraries(midifile_read_test PRIVATE libremidi Catch2::Catch2WithMain)
target_compile_definitions(midifile_read_test PRIVATE "LIBREMIDI_TEST_CORPUS=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus\"")

add_executable(midifile_write_test tests/unit/midifile_write.cpp)
target_link_libraries(midifile_write_test PRIVATE libremidi Catch2::Catch2WithMain)
target_compile_definitions(midifile_write_test PRIVATE "LIBREMIDI_TEST_CORPUS=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus\"")

add_executable(midifile_write_tracks_test tests/integration/midifile_write_tracks.cpp)
target_link_libraries(midifile_write_tracks_test PRIVATE libremidi Catch2::Catch2WithMain)

include(CTest)
add_test(NAME error_test COMMAND error_test)
add_test(NAME midiin_test COMMAND midiin_test)
add_test(NAME midiout_test COMMAND midiout_test)
add_test(NAME midifile_read_test COMMAND midifile_read_test)
add_test(NAME midifile_write_test COMMAND midifile_write_test)
add_test(NAME midifile_write_tracks_test COMMAND midifile_write_tracks_test)
