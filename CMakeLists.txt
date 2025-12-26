
cmake_minimum_required(VERSION 3.10)
project(sdl3-synth CXX)

set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON CACHE INTERNAL "" FORCE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
    GIT_REPOSITORY https://github.com/atsushieno/libremidi.git
    GIT_TAG        master
    GIT_SHALLOW 1
)
FetchContent_MakeAvailable(libremidi)

FetchContent_Declare(
    cjson
    GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
    GIT_TAG        master
    GIT_SHALLOW 1
    CMAKE_ARGS -DENABLE_CUSTOM_COMPILER_FLAGS=ON -DENABLE_CJSON_UTILS=OFF -DBUILD_TESTS=OFF -DENABLE_CJSON_TEST=OFF
)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(cjson)

# Force disable cjson tests for Emscripten
if(EMSCRIPTEN)
    set(ENABLE_CJSON_TEST OFF CACHE BOOL "Disable cjson tests for Emscripten" FORCE)
endif()

if(TARGET cjson)
    target_compile_options(cjson PRIVATE -Wno-error -Wno-long-long -DENABLE_CUSTOM_COMPILER_FLAGS=ON -DENABLE_CJSON_TEST=OFF -DENABLE_CJSON_UTILS=OFF)
endif()

if(NOT EMSCRIPTEN)
    find_package(OpenGL REQUIRED)
endif()

add_executable(sdl3-synth WIN32 main.cpp Oscillator.cpp Voice.cpp Synthesizer.cpp Preset.cpp Utils.cpp Filter.cpp Melody.cpp SineTable.cpp)

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
            ${CMAKE_BINARY_DIR}/_deps/cjson-build/libcjson.dll
            ${CMAKE_BINARY_DIR}/_deps/libremidi-build/liblibremidi.dll
            C:/msys64/mingw64/bin/libgcc_s_seh-1.dll
            C:/msys64/mingw64/bin/libwinpthread-1.dll
            C:/msys64/mingw64/bin/libstdc++-6.dll
            $<TARGET_FILE_DIR:sdl3-synth>
    )
endif()

target_include_directories(sdl3-synth PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${libremidi_SOURCE_DIR}/include ${cjson_SOURCE_DIR})

target_sources(sdl3-synth PRIVATE
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
