# Pipe the names of the host's private test ports to a separately initialized
# client process. Test both exit codes: a failed host must not look like success.
execute_process(COMMAND "${PROGRAM}" host COMMAND "${PROGRAM}" client
                RESULTS_VARIABLE results TIMEOUT 120)
if(NOT "${results}" STREQUAL "0;0")
  message(FATAL_ERROR "WinMIDI virtual host/client failed: ${results}")
endif()
