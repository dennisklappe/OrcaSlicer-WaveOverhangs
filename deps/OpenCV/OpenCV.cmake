# Intel IPP / IPP-ICV is x86/x64 only — there is no ARM64 build, so enabling it
# leaves ~200 unresolved ippicv* externals at link time on Windows ARM64.
if (MSVC AND NOT "${DEPS_ARCH}" STREQUAL "arm64")
    set(_use_IPP "-DWITH_IPP=ON")
    if (DEP_DEBUG)
        set(_options "FORWARD_CONFIG")
    endif ()
else ()
    set(_use_IPP "-DWITH_IPP=OFF")
    set(_options "")
endif ()

if (IN_GIT_REPO)
    set(OpenCV_DIRECTORY_FLAG --directory ${BINARY_DIR_REL}/dep_OpenCV-prefix/src/dep_OpenCV)
endif ()

# When arm64 deps are configured by an x86_64 CMake (e.g. an Intel-Homebrew
# CMake running under Rosetta), OpenCV's host-based CPU detection leaves its
# AARCH64 flag unset, so its bundled libpng never adds the arm/*neon*.c sources.
# The arm64 compiler still defines __ARM_NEON, so libpng's headers emit NEON
# *references* (png_*_neon) with no implementation -> the final link fails with
# undefined symbols. Force libpng to its portable C paths, but only in that
# mismatched host/target setup: native arm64 toolchains (including CI) detect
# AARCH64 correctly and keep the NEON fast paths untouched. DEPS_ARCH is only
# assigned in the MSVC branch of deps/CMakeLists.txt, so on APPLE it is empty;
# key off the target CMAKE_OSX_ARCHITECTURES instead. The flag must ride on
# CMAKE_C_FLAGS (not OPENCV_EXTRA_C_FLAGS, which OpenCV 4.6.0 resets to "" at
# configure time) so it reaches the bundled libpng C sources as a real define.
if (APPLE AND CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "x86_64" AND CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
    set(_opencv_libpng_no_neon "-DCMAKE_C_FLAGS=-DPNG_ARM_NEON_OPT=0")
endif ()

orcaslicer_add_cmake_project(OpenCV
    ${_options}
    URL https://github.com/opencv/opencv/archive/refs/tags/4.6.0.tar.gz
    URL_HASH SHA256=1ec1cba65f9f20fe5a41fda1586e01c70ea0c9a6d7b67c9e13edf0cfe2239277
    PATCH_COMMAND git apply ${OpenCV_DIRECTORY_FLAG} --verbose --ignore-space-change --whitespace=fix ${CMAKE_CURRENT_LIST_DIR}/0001-vs.patch  ${CMAKE_CURRENT_LIST_DIR}/0002-clang19-macos.patch
    CMAKE_ARGS
       # Empty unless the x86_64-host / arm64-target mismatch above is detected.
       ${_opencv_libpng_no_neon}
    -DBUILD_SHARED_LIBS=0
       -DBUILD_PERE_TESTS=OFF
       -DBUILD_TESTS=OFF
       -DBUILD_opencv_python_tests=OFF
       -DBUILD_EXAMPLES=OFF
       -DBUILD_JASPER=OFF
       -DBUILD_JAVA=OFF
       -DBUILD_JPEG=ON
       -DBUILD_APPS_LIST=version
       -DBUILD_opencv_apps=OFF
       -DBUILD_opencv_java=OFF
       -DBUILD_OPENEXR=OFF
       -DBUILD_PNG=ON
       -DBUILD_TBB=OFF
       -DBUILD_WEBP=OFF
       -DBUILD_ZLIB=OFF
       -DWITH_1394=OFF
       -DWITH_CUDA=OFF
       -DWITH_EIGEN=OFF
       ${_use_IPP}
       -DWITH_ITT=OFF
       -DWITH_FFMPEG=OFF
       -DWITH_GPHOTO2=OFF
       -DWITH_GSTREAMER=OFF
       -DOPENCV_GAPI_GSTREAMER=OFF
       -DWITH_GTK_2_X=OFF
       -DWITH_JASPER=OFF
       -DWITH_LAPACK=OFF
       -DWITH_MATLAB=OFF
       -DWITH_MFX=OFF
       -DWITH_DIRECTX=OFF
       -DWITH_DIRECTML=OFF
       -DWITH_OPENCL=OFF
       -DWITH_OPENCL_D3D11_NV=OFF
       -DWITH_OPENCLAMDBLAS=OFF
       -DWITH_OPENCLAMDFFT=OFF
       -DWITH_OPENEXR=OFF
       -DWITH_OPENJPEG=OFF
       -DWITH_QUIRC=OFF
       -DWITH_VTK=OFF
       -DWITH_JPEG=OFF
       -DWITH_WEBP=OFF
       -DWITH_TIFF=OFF
       -DBUILD_TIFF=OFF
       -DENABLE_PRECOMPILED_HEADERS=OFF
       -DINSTALL_TESTS=OFF
       -DINSTALL_C_EXAMPLES=OFF
       -DINSTALL_PYTHON_EXAMPLES=OFF
       -DOPENCV_GENERATE_SETUPVARS=OFF
       -DOPENCV_INSTALL_FFMPEG_DOWNLOAD_SCRIPT=OFF
       -DBUILD_opencv_python2=OFF
       -DBUILD_opencv_python3=OFF
       -DWITH_OPENVINO=OFF
       -DWITH_INF_ENGINE=OFF
       -DWITH_NGRAPH=OFF
       -DBUILD_WITH_STATIC_CRT=OFF#set /MDd /MD
       -DBUILD_LIST=core,imgcodecs,imgproc,world
       -DBUILD_opencv_highgui=OFF
       -DWITH_ADE=OFF
       -DBUILD_opencv_world=ON
       -DWITH_PROTOBUF=OFF
       -DWITH_WIN32UI=OFF
       -DHAVE_WIN32UI=FALSE
)

