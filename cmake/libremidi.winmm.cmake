message(STATUS "libremidi: using WinMM")
set(LIBREMIDI_HAS_WINMM 1)
target_compile_definitions(libremidi
  ${_public}
    LIBREMIDI_WINMM
    UNICODE=1
    _UNICODE=1
)
target_link_libraries(libremidi ${_public} winmm)
