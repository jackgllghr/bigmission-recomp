# Puts what builds the game on the phone into the APK, at build time (cmake -P):
# the code generator and the shipped clang among the native libraries, where
# an app may run programs, and the SDK the module is built against (the
# runtime's headers, the module's entry, the compiler's sysroot) as one
# archive among the assets, which the activity unpacks.
#
# -DSDK=<bin/sdk> -DCODE_GENERATOR=<psp_recomp> -DTOOLCHAIN=<build_toolchain.py's stage>
# -DJNILIBS=<the ABI's jniLibs directory> -DASSETS=<assets directory> -DWORK=<scratch directory>

file(REMOVE_RECURSE "${WORK}")
file(COPY "${SDK}/" DESTINATION "${WORK}/sdk")
file(MAKE_DIRECTORY "${JNILIBS}")
file(COPY_FILE "${CODE_GENERATOR}" "${JNILIBS}/libpsp_recomp.so" ONLY_IF_DIFFERENT)
if(EXISTS "${TOOLCHAIN}/jni/libsarusaru_llvm.so" AND EXISTS "${TOOLCHAIN}/sysroot")
    file(COPY_FILE "${TOOLCHAIN}/jni/libsarusaru_llvm.so" "${JNILIBS}/libsarusaru_llvm.so" ONLY_IF_DIFFERENT)
    file(COPY "${TOOLCHAIN}/sysroot/" DESTINATION "${WORK}/sdk/sysroot")
else()
    file(REMOVE "${JNILIBS}/libsarusaru_llvm.so")
    message(WARNING "sarusaru: no compiler for the phone in ${TOOLCHAIN}; this APK cannot install the game "
        "(run packaging/android/toolchain/build_toolchain.py)")
endif()
file(MAKE_DIRECTORY "${ASSETS}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${ASSETS}/sdk.zip" --format=zip sdk
    WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "sarusaru: cannot archive the SDK for the APK")
endif()
