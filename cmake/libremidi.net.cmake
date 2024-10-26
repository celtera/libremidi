if(NOT TARGET Boost::boost)
  return()
endif()

message(STATUS "libremidi: Network support using Boost.ASIO")
set(LIBREMIDI_HAS_BOOST_ASIO 1)
set(LIBREMIDI_HAS_NETWORK 1)

target_compile_definitions(libremidi
  ${_public}
    LIBREMIDI_NETWORK
)
target_link_libraries(libremidi
  ${_public}
    $<BUILD_INTERFACE:Boost::boost>
)
