if(NOT LIBREMIDI_HEADER_ONLY)
  install(TARGETS libremidi
          EXPORT libremidi-targets
          ARCHIVE
          RUNTIME
          LIBRARY
  )
else()
  install(TARGETS libremidi
          EXPORT libremidi-targets
  )
endif()
install(EXPORT libremidi-targets
        DESTINATION lib/cmake/libremidi)
install(DIRECTORY include
        DESTINATION .)

include(CMakePackageConfigHelpers)

# generate the config file that includes the exports
configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/libremidi-config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/libremidi-config.cmake"
  INSTALL_DESTINATION "lib/cmake/libremidi"
  NO_SET_AND_CHECK_MACRO
  NO_CHECK_REQUIRED_COMPONENTS_MACRO
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/libremidi-config-version.cmake"
  VERSION "${CMAKE_PROJECT_VERSION}"
  COMPATIBILITY AnyNewerVersion
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/libremidi-config.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/libremidi-config-version.cmake
  DESTINATION lib/cmake/libremidi
)
