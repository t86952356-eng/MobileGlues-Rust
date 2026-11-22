cmake_minimum_required(VERSION 3.22.1)
project("mobileglues")
enable_language(CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)


# glslang & SPIRV-Cross
set(ENABLE_RTTI                ON  CACHE BOOL "Enables RTTI (will be took by glslang)" FORCE)
set(ENABLE_HLSL                OFF CACHE BOOL "Disable HLSL input for glslang" FORCE)
set(BUILD_EXTERNAL             OFF CACHE BOOL "Disable build external dependencies for glslang" FORCE)
set(ENABLE_GLSLANG_BINARIES    OFF CACHE BOOL "Disable glslangValidator/spirv-remap" FORCE)
set(ENABLE_OPT                 OFF CACHE BOOL "Disable SPIRV-Tools opt usage in glslang" FORCE)
set(BUILD_EXTERNAL             OFF CACHE BOOL "Don't build external deps in External/" FORCE)

set(SPIRV_CROSS_ENABLE_C_API   ON  CACHE BOOL "Enable C API" FORCE)
set(SPIRV_CROSS_ENABLE_GLSL    ON  CACHE BOOL "Enable GLSL backend" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "Disable reflect" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL    OFF CACHE BOOL "Disable HLSL backend" FORCE)
set(SPIRV_CROSS_ENABLE_MSL     OFF CACHE BOOL "Disable MSL backend" FORCE)
set(SPIRV_CROSS_ENABLE_CPP     OFF CACHE BOOL "Disable C++ API target" FORCE)
set(SPIRV_CROSS_CLI            OFF CACHE BOOL "Disable CLI binary" FORCE)
set(SPIRV_CROSS_STATIC         ON  CACHE BOOL "Prefer static libs" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL    OFF CACHE BOOL "Disable util" FORCE)

add_subdirectory(3rdparty/glslang)
add_subdirectory(3rdparty/SPIRV-Cross)

if(MACOS)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w -fvisibility=default -funwind-tables -g -D_THREAD_SAFE -fPIC -stdlib=libc++")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w -g -std=gnu99 -funwind-tables -fvisibility=hidden")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w -fvisibility=hidden -funwind-tables -g -D_THREAD_SAFE -fPIC")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w -g -std=gnu99 -funwind-tables -fvisibility=hidden")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++")
    set(CMAKE_ANDROID_STL_TYPE c++_static)
endif()

if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Check if ThinLTO or LTO is suppported
    include(CheckIPOSupported)
    include(CheckCCompilerFlag)
    include(CheckCXXCompilerFlag)

    check_ipo_supported(RESULT LTOSupported OUTPUT LTOError)

    check_c_compiler_flag("-flto" HAS_LTO_C)
    check_cxx_compiler_flag("-flto" HAS_LTO_CXX)

    if (LTOSupported OR (HAS_LTO_C AND HAS_LTO_CXX))
    # Check ThinLTO
    check_c_compiler_flag("-flto=thin" HAS_THINLTO_C)
    check_cxx_compiler_flag("-flto=thin" HAS_THINLTO_CXX)
    if (HAS_THINLTO_C AND HAS_THINLTO_CXX)
        message(STATUS "ThinLTO supported, using -flto=thin")

        add_compile_options(-flto=thin)
        add_link_options(-flto=thin)
    else()
        # ThinLTO is not supported
        message(STATUS "ThinLTO not available, fallback to CMAKE IPO")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
    else()
    message(STATUS "IPO not supported: ${LTOError}")
    endif()
endif()


if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT MATCHES "AppleClang")
    add_compile_options(-O3 -ffunction-sections -fdata-sections)
    add_link_options(-Wl,--gc-sections)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
#    add_compile_options(/O2)
else ()
    add_compile_options(-O2)
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    add_compile_options(/Zc:preprocessor)
    add_compile_options(/Zc:__cplusplus)
endif()

set(PROFILING OFF)

add_library(${CMAKE_PROJECT_NAME} SHARED
    init.cpp
    main.cpp
    gl/gl_stub.cpp
    gl/gl_native.cpp
    gl/gl.cpp
    gl/envvars.cpp
    gl/log.cpp
    gl/program.cpp
    gl/shader.cpp
    gl/framebuffer.cpp
    gl/texture.cpp
    gl/drawing.cpp
    gl/multidraw.cpp
    bench/multidraw_bench.cpp
    gl/mg.cpp
    gl/buffer.cpp
    gl/getter.cpp
    gl/enable.cpp
    gl/restart.cpp
    gl/perf.cpp
    egl/context.cpp
    gl/pixel.cpp
    gl/transfer.cpp
    gl/random_string_gen.cpp
    gl/ExtWrappers/DSAWrapper.cpp
    gl/ExtWrappers/MultiBindWrapper.cpp
    gl/glsl/glsl_for_es.cpp
    gl/glsl/cache.cpp
    gl/glsl/rs_bridge.cpp
    gl/FSR1/FSR1.cpp

    gl/vertexattrib.cpp
    glx/lookup.cpp
    egl/egl.cpp
    egl/loader.cpp
    gles/loader.cpp
    config/cJSON.c
    config/config.cpp
    config/gpu_utils.cpp
    config/settings.cpp
    config/stats.cpp
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ./includes
    ./3rdparty/xxhash)
target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    glslang::glslang
    spirv-cross-c
)

# ----------------------------------------------------------------------------
# mobileglues-rs — Rust text-processing core
#
# Builds the rust/ crate with cargo for the current Android ABI and links the
# resulting static library into libmobileglues.so. The C++ side reaches it
# through weak symbols (gl/glsl/rs_bridge.h), so a build without cargo
# (MG_ENABLE_RUST=OFF, or cargo not found) still produces a fully working
# library that runs the original C++ implementations.
# ----------------------------------------------------------------------------
set(MG_ENABLE_RUST ON CACHE BOOL "Build and link the mobileglues-rs Rust core")

if(MG_ENABLE_RUST)
    find_program(MG_CARGO_EXE cargo)
    if(NOT MG_CARGO_EXE)
        message(WARNING "cargo not found — building WITHOUT the Rust core; the C++ fallback paths will be used")
        set(MG_ENABLE_RUST OFF)
    endif()
endif()

# Resolve the Rust target triple for the platform being built.
if(MG_ENABLE_RUST)
    set(MG_RS_TRIPLE "")
    if(ANDROID)
        if(ANDROID_ABI STREQUAL "arm64-v8a")
            set(MG_RS_TRIPLE aarch64-linux-android)
        elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
            set(MG_RS_TRIPLE armv7-linux-androideabi)
        elseif(ANDROID_ABI STREQUAL "x86_64")
            set(MG_RS_TRIPLE x86_64-linux-android)
        elseif(ANDROID_ABI STREQUAL "x86")
            set(MG_RS_TRIPLE i686-linux-android)
        endif()
    elseif(UNIX AND NOT APPLE)
        # Host Linux build (used for development and the rsdiff host check).
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
            set(MG_RS_TRIPLE x86_64-unknown-linux-gnu)
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
            set(MG_RS_TRIPLE aarch64-unknown-linux-gnu)
        endif()
    endif()
    if(ANDROID AND NOT MG_RS_TRIPLE)
        message(WARNING "Unknown ANDROID_ABI '${ANDROID_ABI}' — building WITHOUT the Rust core")
    endif()
endif()

if(MG_ENABLE_RUST AND MG_RS_TRIPLE)
    if(ANDROID)
        # API level: from ANDROID_PLATFORM (e.g. "android-21"), defaulting to
        # 21 (the AAR minSdk) when the variable is absent.
        string(REGEX REPLACE "android-" "" _rs_api "${ANDROID_PLATFORM}")
        if(NOT _rs_api MATCHES "^[0-9]+$")
            set(_rs_api 21)
        endif()

        # Cargo needs a linker executable; point it at a tiny wrapper that
        # calls the NDK clang with the right --target so we do not have to
        # guess the per-triple wrapper script names for every NDK revision.
        get_filename_component(_rs_clang_dir "${CMAKE_C_COMPILER}" DIRECTORY)
        set(_rs_linker "${CMAKE_CURRENT_BINARY_DIR}/rust-linker-${MG_RS_TRIPLE}.sh")
        file(WRITE "${_rs_linker}" "#!/bin/sh\nexec \"${_rs_clang_dir}/clang\" --target=${MG_RS_TRIPLE}${_rs_api} \"\$@\"\n")
        file(CHMOD "${_rs_linker}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    else()
        # Host build: the project's own C++ compiler links the Rust core.
        set(_rs_linker "${CMAKE_CXX_COMPILER}")
    endif()

    string(TOUPPER "${MG_RS_TRIPLE}" _rs_triple_upper)
    string(REPLACE "-" "_" _rs_linker_env "CARGO_TARGET_${_rs_triple_upper}_LINKER")

    set(MG_RS_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/rust-target")
    set(MG_RS_LIB "${MG_RS_TARGET_DIR}/${MG_RS_TRIPLE}/release/libmobileglues_rs.a")

    file(GLOB_RECURSE MG_RS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/rust/src/*.rs")
    add_custom_command(
        OUTPUT "${MG_RS_LIB}"
        COMMAND "${CMAKE_COMMAND}" -E env
            "${_rs_linker_env}=${_rs_linker}"
            "${MG_CARGO_EXE}" build --release --target "${MG_RS_TRIPLE}"
                --target-dir "${MG_RS_TARGET_DIR}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/rust"
        DEPENDS ${MG_RS_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/rust/Cargo.toml"
        COMMENT "Building mobileglues-rs (${MG_RS_TRIPLE})"
        VERBATIM
        USES_TERMINAL
    )

    add_library(mobileglues_rs STATIC IMPORTED)
    set_target_properties(mobileglues_rs PROPERTIES
        IMPORTED_LOCATION "${MG_RS_LIB}"
        IMPORTED_NO_SONAME TRUE
    )
    add_custom_target(mobileglues_rs_build DEPENDS "${MG_RS_LIB}")
    add_dependencies(${CMAKE_PROJECT_NAME} mobileglues_rs_build)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE mobileglues_rs)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE MG_RS_INTEGRATED=1)
    message(STATUS "mobileglues-rs: enabled for ${MG_RS_TRIPLE}")
else()
    message(STATUS "mobileglues-rs: disabled (platform without a triple or cargo unavailable); C++ fallback paths will be used")
endif()

if (NOT MSVC)
    # Calls between this library's own translation units must land in this
    # library. Without this, an internal call to an exported gl* symbol goes
    # through a preemptible PLT slot, and in an Android app process the system
    # libGLESv2 -- always in the global symbol scope -- can win the lookup: the
    # DSA wrappers and the benchmark would then be calling the real driver where
    # they meant the frontend, silently bypassing all state tracking. The
    # symbols stay exported for dlsym and eglGetProcAddress; only who internal
    # calls bind to changes. On the target, not in the AppleClang-guarded block
    # above -- that block's condition has never been true (dangling NOT MATCHES),
    # which is also why --gc-sections never made it into the link.
    target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -Wl,-Bsymbolic-functions)
endif()

if(MACOS)
    # iOS
    target_link_libraries(${CMAKE_PROJECT_NAME} PUBLIC
        "-F${CMAKE_SOURCE_DIR}/external"
        "-framework libEGL"
        "-framework libGLESv2"
        "-framework Metal"
        "-framework Foundation"
        "-framework QuartzCore"
    )
elseif(ANDROID)
    # Android
    target_link_libraries(${CMAKE_PROJECT_NAME} PUBLIC
        android
        log
    )
    else()
    # Linux — EGL/GLES are loaded dynamically via dlopen
    target_link_libraries(${CMAKE_PROJECT_NAME} PUBLIC
        ${CMAKE_DL_LIBS}
    )
endif()

if (PROFILING)
    find_package(Threads)
    include_directories(${CMAKE_SOURCE_DIR}/3rdparty/perfetto/sdk)
    add_library(perfetto STATIC ${CMAKE_SOURCE_DIR}/3rdparty/perfetto/sdk/perfetto.cc)
    target_link_libraries(${CMAKE_PROJECT_NAME} perfetto ${CMAKE_THREAD_LIBS_INIT})
    target_compile_definitions(${CMAKE_PROJECT_NAME} PUBLIC PROFILING=1)
endif()
