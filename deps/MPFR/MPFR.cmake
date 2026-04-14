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

    # On most Linux distros GMP installs into lib64/ (autoconf default) so
    # MPFR's plain --with-gmp=PREFIX lookup misses it; pass explicit lib/include
    # paths + --libdir so MPFR itself lands alongside GMP. macOS and other
    # platforms use the original plain --with-gmp=PREFIX path.
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(_mpfr_gmp_args --with-gmp-lib=${DESTDIR}/lib64 --with-gmp-include=${DESTDIR}/include --libdir=${DESTDIR}/lib64)
    else ()
        set(_mpfr_gmp_args --with-gmp=${DESTDIR})
    endif ()

    ExternalProject_Add(dep_MPFR
        URL https://ftp.gnu.org/gnu/mpfr/mpfr-4.2.2.tar.bz2
            https://www.mpfr.org/mpfr-4.2.2/mpfr-4.2.2.tar.bz2
        URL_HASH SHA256=9ad62c7dc910303cd384ff8f1f4767a655124980bb6d8650fe62c815a231bb7b
        DOWNLOAD_DIR ${DEP_DOWNLOAD_DIR}/MPFR
        BUILD_IN_SOURCE ON
        CONFIGURE_COMMAND autoreconf -f -i &&
                          env "CC=${CMAKE_C_COMPILER}" "CXX=${CMAKE_CXX_COMPILER}" "CFLAGS=${_gmp_ccflags}" "CXXFLAGS=${_gmp_ccflags}" "LDFLAGS=${CMAKE_EXE_LINKER_FLAGS}" ./configure ${_cross_compile_arg} --prefix=${DESTDIR} --enable-shared=no --enable-static=yes ${_mpfr_gmp_args} ${_gmp_build_tgt}
        BUILD_COMMAND make -j
        INSTALL_COMMAND make install
        DEPENDS dep_GMP
    )
endif ()
