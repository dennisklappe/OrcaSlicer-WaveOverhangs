set(_srcdir ${CMAKE_CURRENT_LIST_DIR}/mpfr)

if (MSVC)
    set(_output  ${DESTDIR}/include/mpfr.h
                 ${DESTDIR}/include/mpf2mpfr.h
                 ${DESTDIR}/lib/libmpfr-4.lib 
                 ${DESTDIR}/bin/libmpfr-4.dll)

    add_custom_command(
        OUTPUT  ${_output}
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/include/mpfr.h ${DESTDIR}/include/
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/include/mpf2mpfr.h ${DESTDIR}/include/
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/lib/win-${DEPS_ARCH}/libmpfr-4.lib ${DESTDIR}/lib/
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/lib/win-${DEPS_ARCH}/libmpfr-4.dll ${DESTDIR}/bin/
    )

    add_custom_target(dep_MPFR SOURCES ${_output})

else ()

    set(_cross_compile_arg "")
    if (CMAKE_CROSSCOMPILING)
        # TOOLCHAIN_PREFIX should be defined in the toolchain file
        set(_cross_compile_arg --host=${TOOLCHAIN_PREFIX})
    endif ()

    # Write a tiny wrapper script that detects whether GMP landed in lib/ or
    # lib64/ at MPFR-configure time (after dep_GMP installed), then invokes
    # autoreconf + configure with the right --with-gmp-lib and --libdir. Using
    # a script file avoids CMake/sh quoting pitfalls that arose with an inline
    # `sh -c "..."` command.
    # Flatten CMake lists (e.g. Apple's _gmp_build_tgt = "--build=...;--host=...")
    # to space-separated strings. Without this the generated shell script would
    # see `;` as a statement separator and try to exec the second flag as a command.
    string(REPLACE ";" " " _mpfr_build_tgt_str "${_gmp_build_tgt}")
    string(REPLACE ";" " " _mpfr_cross_arg_str "${_cross_compile_arg}")
    set(_mpfr_configure_script ${CMAKE_CURRENT_BINARY_DIR}/configure_mpfr.sh)
    file(WRITE ${_mpfr_configure_script}
"#!/bin/bash
set -e
DESTDIR='${DESTDIR}'
GMP_LIBDIR=lib
for ext in a so dylib; do
  if [ -f \"$DESTDIR/lib64/libgmp.$ext\" ]; then GMP_LIBDIR=lib64; break; fi
done
echo \"MPFR: using GMP from $DESTDIR/$GMP_LIBDIR\"
autoreconf -f -i
env CC='${CMAKE_C_COMPILER}' CXX='${CMAKE_CXX_COMPILER}' CFLAGS='${_gmp_ccflags}' CXXFLAGS='${_gmp_ccflags}' LDFLAGS='${CMAKE_EXE_LINKER_FLAGS}' ./configure ${_mpfr_cross_arg_str} --prefix='${DESTDIR}' --enable-shared=no --enable-static=yes --with-gmp-lib=\"$DESTDIR/$GMP_LIBDIR\" --with-gmp-include=\"$DESTDIR/include\" --libdir=\"$DESTDIR/$GMP_LIBDIR\" ${_mpfr_build_tgt_str}
")
    ExternalProject_Add(dep_MPFR
        URL https://ftp.gnu.org/gnu/mpfr/mpfr-4.2.2.tar.bz2
            https://www.mpfr.org/mpfr-4.2.2/mpfr-4.2.2.tar.bz2
        URL_HASH SHA256=9ad62c7dc910303cd384ff8f1f4767a655124980bb6d8650fe62c815a231bb7b
        DOWNLOAD_DIR ${DEP_DOWNLOAD_DIR}/MPFR
        BUILD_IN_SOURCE ON
        CONFIGURE_COMMAND bash ${_mpfr_configure_script}
        BUILD_COMMAND make -j
        INSTALL_COMMAND make install
        DEPENDS dep_GMP
    )
endif ()
