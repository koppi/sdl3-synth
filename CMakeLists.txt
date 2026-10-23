cmake_minimum_required(VERSION 3.25)

# A build type is required: the VST3 SDK generator expressions and the validator
# integration only make sense for a single-configuration build. Default to
# Release when the caller did not choose one (single-config generators only).
if(WIN32 AND NOT CMAKE_CONFIGURATION_TYPES)
    # Ninja/Makefiles on Windows: keep a sane default.
endif()
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

project(sdl3-synth VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON CACHE INTERNAL "" FORCE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SDL3_SYNTH_BUILD_VST3 "Build the VST3 plug-in" ON)

# The VST3 plug-in is a shared module, so every static dependency (notably SDL3)
# must be compiled as position independent code.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# The web build and Debian package builds have no place to put a VST3 bundle.
if(EMSCRIPTEN)
    set(SDL3_SYNTH_BUILD_VST3 OFF)
endif()

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pg -ffast-math -march=native")
#set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pg")

set(FETCHCONTENT_QUIET FALSE)
include(FetchContent)

if(EMSCRIPTEN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -matomics -mbulk-memory -s DISABLE_EXCEPTION_CATCHING=0")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -matomics -mbulk-memory -s DISABLE_EXCEPTION_CATCHING=0")
endif()

FetchContent_Declare(
  SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG        main
  GIT_SHALLOW 1
  CMAKE_ARGS -DCMAKE_C_FLAGS:STRING="-matomics -mbulk-memory -pthread -msimd128 -s DISABLE_EXCEPTION_CATCHING=0" -DCMAKE_CXX_FLAGS:STRING="-matomics -mbulk-memory -pthread -msimd128 -s DISABLE_EXCEPTION_CATCHING=0"
)
set(SDL_SHARED OFF)
set(SDL_STATIC ON)
set(SDL_STATIC_PIC ON)
set(SDL_TEST OFF)
set(SDL_TESTS OFF)
set(SDL_X11_XTEST OFF)
FetchContent_MakeAvailable(SDL3)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        master
    GIT_SHALLOW 1
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
    libremidi
    GIT_REPOSITORY https://github.com/celtera/libremidi.git
    GIT_TAG        master
    GIT_SHALLOW 1
    CMAKE_ARGS -DLIBREMIDI_USE_WinRT=OFF -DLIBREMIDI_NO_WINUWP=ON -DLIBREMIDI_JACK=OFF
)
set(LIBREMIDI_JACK OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(libremidi)

FetchContent_Declare(
    cjson
    GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
    GIT_TAG        master
    GIT_SHALLOW 1
    # Both variants are built: the application links the shared one, the VST3
    # plug-in the static one, so that the plug-in does not depend on a cJSON
    # runtime library being installed next to it.
    CMAKE_ARGS -DENABLE_CUSTOM_COMPILER_FLAGS=ON -DENABLE_CJSON_UTILS=OFF -DBUILD_TESTS=OFF -DENABLE_CJSON_TEST=OFF -DBUILD_SHARED_AND_STATIC_LIBS=ON
)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(cjson)

# Force disable cjson tests for Emscripten
if(EMSCRIPTEN)
    set(ENABLE_CJSON_TEST OFF CACHE BOOL "Disable cjson tests for Emscripten" FORCE)
endif()

foreach(cjson_target cjson cjson-static)
    if(TARGET ${cjson_target})
        target_compile_definitions(${cjson_target} PRIVATE ENABLE_CUSTOM_COMPILER_FLAGS=ON ENABLE_CJSON_TEST=OFF ENABLE_CJSON_UTILS=OFF)
        if(NOT MSVC)
            target_compile_options(${cjson_target} PRIVATE -Wno-error -Wno-long-long)
        endif()
    endif()
endforeach()

if(NOT EMSCRIPTEN)
    find_package(OpenGL REQUIRED)
endif()

# ---------------------------------------------------------------------------
# Shared sources
# ---------------------------------------------------------------------------
set(SDL3_SYNTH_CORE_SOURCES
    Engine.cpp
    Gui.cpp
    Oscillator.cpp
    Voice.cpp
    Synthesizer.cpp
    Preset.cpp
    Utils.cpp
    Filter.cpp
    Melody.cpp
    SineTable.cpp
)

set(SDL3_SYNTH_IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

# ---------------------------------------------------------------------------
# Standalone application
# ---------------------------------------------------------------------------
add_executable(sdl3-synth WIN32 main.cpp ${SDL3_SYNTH_CORE_SOURCES})

if(EMSCRIPTEN)
    set(CMAKE_CXX_COMPILER emcc)
    target_include_directories(sdl3-synth PRIVATE ${SDL3_SOURCE_DIR}/include)
    target_compile_options(sdl3-synth PRIVATE -matomics -mbulk-memory)
    target_compile_definitions(sdl3-synth PRIVATE IMGUI_DISABLE_TTY_FUNCTIONS)

    # Copy data directory to build directory as a pre-build step
    add_custom_command(TARGET sdl3-synth PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data ${CMAKE_BINARY_DIR}/data
        COMMENT "Copying data directory to build directory"
    )

    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lGL -s DISABLE_EXCEPTION_CATCHING=0 -s MIN_WEBGL_VERSION=2 -s MAX_WEBGL_VERSION=2 -s WASM=1 -s USE_WEBGL2=1 -s USE_GLFW=3 -s FULL_ES3=1 -s ASYNCIFY=1 -s ASYNCIFY_IMPORTS=['emscripten_sleep'] -s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPU8'] -s EXPORTED_FUNCTIONS=['_malloc','_free','_libremidi_devices_poll','libremidi_devices_input'] -s ALLOW_MEMORY_GROWTH=1 -s FORCE_FILESYSTEM=0 -s EXIT_RUNTIME=1 -O2 --preload-file data --pre-js pre.js")
    target_link_libraries(sdl3-synth PRIVATE SDL3::SDL3 cjson libremidi::libremidi)
    add_custom_command(TARGET sdl3-synth POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/index.html ${CMAKE_BINARY_DIR}/index.html
      )

  add_custom_command(TARGET sdl3-synth PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/pre.js
    ${CMAKE_BINARY_DIR}/pre.js
    COMMENT "Copying pre.js to build directory"
  )

  add_custom_command(TARGET sdl3-synth POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/index.html
    ${CMAKE_BINARY_DIR}/index.html
    COMMENT "Copying index.html to build directory"
  )

  add_custom_command(TARGET sdl3-synth POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/favicon.ico
    ${CMAKE_BINARY_DIR}/favicon.ico
    COMMENT "Copying favicon.ico to build directory"
  )
else()
    target_link_libraries(sdl3-synth PRIVATE SDL3::SDL3 OpenGL::GL libremidi::libremidi cjson)

    # Copy data directory to build directory as a post-build step
    add_custom_command(TARGET sdl3-synth POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data ${CMAKE_BINARY_DIR}/data
        COMMENT "Copying data directory to build directory"
    )
endif()

# Copy required DLLs to build directory (Windows only)
if(WIN32)
    add_custom_command(TARGET sdl3-synth POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:cjson>
            $<TARGET_FILE:libremidi::libremidi>
            $<TARGET_FILE_DIR:sdl3-synth>
    )
endif()

target_include_directories(sdl3-synth PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${libremidi_SOURCE_DIR}/include ${cjson_SOURCE_DIR})

target_sources(sdl3-synth PRIVATE ${SDL3_SYNTH_IMGUI_SOURCES})

# ---------------------------------------------------------------------------
# VST3 plug-in
# ---------------------------------------------------------------------------
if(SDL3_SYNTH_BUILD_VST3)
    # Configure the SDK before it is fetched so its options are picked up.
    set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
    set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
    # The editorhost hosting example pulls in GTKmm, which is not needed here.
    # The validator is built regardless (and is what checks the plug-in below).
    set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)

    # The SDK is a superproject: the actual sources live in its submodules.
    # Only the ones needed to build a plug-in are checked out.
    FetchContent_Declare(
        vst3sdk
        GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
        GIT_TAG        v3.8.1_build_84
        GIT_SHALLOW    1
        GIT_SUBMODULES "base" "cmake" "pluginterfaces" "public.sdk"
        GIT_SUBMODULES_RECURSE ON
    )
    FetchContent_MakeAvailable(vst3sdk)

    smtg_enable_vst3_sdk()

    smtg_add_vst3plugin(SDL3Synth
        vst3/SynthVst3.cpp
        vst3/SynthVst3.h
        vst3/SynthEditor.cpp
        vst3/SynthEditor.h
        vst3/entry.cpp
        vst3/gl_loader.h
        vst3/PlugIds.h
        vst3/version.h
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
        ${SDL3_SYNTH_CORE_SOURCES}
        ${SDL3_SYNTH_IMGUI_SOURCES}
    )

    # ImGui's bundled OpenGL loader guesses the GL library to resolve from by
    # inspecting what the host process already has loaded; inside a plug-in that
    # can pick eglGetProcAddress() for a GLX context and yield dummy entry
    # points (shader compilation then fails). We link the GL library directly,
    # so declare the real entry points instead -- for this target only.
    if(MSVC)
        set(SDL3_SYNTH_FORCE_INCLUDE_FLAG "/FI${CMAKE_CURRENT_SOURCE_DIR}/vst3/gl_loader.h")
    else()
        set(SDL3_SYNTH_FORCE_INCLUDE_FLAG "-include" "${CMAKE_CURRENT_SOURCE_DIR}/vst3/gl_loader.h")
    endif()
    set_source_files_properties(
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        TARGET_DIRECTORY SDL3Synth
        PROPERTIES
            COMPILE_DEFINITIONS "IMGUI_IMPL_OPENGL_LOADER_CUSTOM"
            COMPILE_OPTIONS "${SDL3_SYNTH_FORCE_INCLUDE_FLAG}"
    )

    smtg_target_configure_version_file(SDL3Synth)

    smtg_target_add_plugin_resources(SDL3Synth
        RESOURCES
            ${CMAKE_SOURCE_DIR}/data/xkcd-script.ttf
        OUTPUT_SUBDIRECTORY data
    )

    target_include_directories(SDL3Synth PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${cjson_SOURCE_DIR}
    )

    # vstsinglecomponenteffect.cpp provides EditController's editor-state stubs
    # itself because public.sdk already compiles vsteditcontroller.cpp into the
    # static 'sdk' library.
    target_compile_definitions(SDL3Synth PRIVATE PROJECT_INCLUDES_VSTEDITCONTROLLER=1)

    if(SDL3_SYNTH_GL_DEBUG)
        target_compile_definitions(SDL3Synth PRIVATE IMGUI_IMPL_OPENGL_DEBUG)
    endif()

    if(MSVC)
        # M_PI is only defined by MSVC's math.h when this macro is set, and
        # UTF-8 source files need the matching code page.
        target_compile_definitions(SDL3Synth PRIVATE _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
        target_compile_options(SDL3Synth PRIVATE /utf-8 /bigobj)
    endif()

    # The static cJSON variant is used on purpose: a plug-in must not depend on
    # a cJSON runtime library being installed next to the bundle.
    if(TARGET cjson-static)
        set(SDL3_SYNTH_VST3_CJSON_TARGET cjson-static)
    else()
        set(SDL3_SYNTH_VST3_CJSON_TARGET cjson)
    endif()

    target_link_libraries(SDL3Synth PRIVATE
        sdk
        SDL3::SDL3
        OpenGL::GL
        ${SDL3_SYNTH_VST3_CJSON_TARGET}
        ${CMAKE_DL_LIBS}
    )

    # The plug-in must not depend on libraries from the build tree.
    set_target_properties(SDL3Synth PROPERTIES
        BUILD_RPATH ""
        INSTALL_RPATH "$ORIGIN"
        BUILD_WITH_INSTALL_RPATH OFF
    )

    # Copy the runtime DLLs the plug-in needs next to it before the SDK's
    # moduleinfotool/validator post-build steps run.
    if(WIN32 AND MINGW)
        add_dependencies(SDL3Synth cjson)
        set(SDL3_SYNTH_MINGW_RUNTIME_DLLS "")
        foreach(runtime_lib libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
            execute_process(
                COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=${runtime_lib}
                OUTPUT_VARIABLE runtime_path
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if(EXISTS "${runtime_path}")
                list(APPEND SDL3_SYNTH_MINGW_RUNTIME_DLLS "${runtime_path}")
            else()
                list(APPEND SDL3_SYNTH_MINGW_RUNTIME_DLLS "${runtime_lib}")
            endif()
        endforeach()

        add_custom_command(TARGET SDL3Synth PRE_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:cjson>
                ${SDL3_SYNTH_MINGW_RUNTIME_DLLS}
                $<TARGET_FILE_DIR:SDL3Synth>
            COMMENT "Copying plug-in runtime DLLs"
        )
    endif()
endif()
