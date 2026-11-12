# Fails when EXECUTABLE defines the runtime's own functions instead of taking
# them from the shared runtime (a static psprecomp_core linked in as well). The
# game module calls the shared runtime, so the host's call hooks, set in its
# private copy, would never run for the game's calls.
#   cmake -DNM=<nm> -DEXECUTABLE=<file> -P check_one_runtime.cmake
execute_process(COMMAND "${NM}" "${EXECUTABLE}" OUTPUT_VARIABLE symbols RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "${NM} failed on ${EXECUTABLE}")
endif()
# set_runtime_pre_chained_call_hook, defined (T) rather than imported (U).
if(symbols MATCHES "[0-9a-fA-F]+ [Tt] _*_ZN9psprecomp33set_runtime_pre_chained_call_hook")
    message(FATAL_ERROR "${EXECUTABLE} has its own copy of the psprecomp runtime; link psprecomp_runtime only")
endif()
