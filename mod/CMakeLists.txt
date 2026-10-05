cmake_minimum_required(VERSION 3.20)
project(SpeederBike LANGUAGES CXX RC)

# Speeder Bike: an ASI plugin for Crimson Desert that adds a speeder bike,
# a ground mount of its own built on the cut broom's skeleton, beside
# Broomy. The file it produces is SpeederBike.asi, and a
# release is that file alone. What is known is in private/KNOWLEDGE.md.

if(NOT WIN32)
    message(FATAL_ERROR "Speeder Bike targets Windows only.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>") # static CRT, no redist needed

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

# MinHook is here only for its HDE64 length decoder, which farhook uses to
# measure the bytes it steals. No MinHook hook is ever created.
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(minhook)

# SpeederBike.ini's own bytes, as a header, so a DMM install (which is the
# plugin alone) can have the documented ini written beside it on first run
# without a second copy of the text going out of step with the first.
# embed_ini.cmake reads and writes HEX on purpose: carrying the file through
# CMake as a string rewrites every LF as CRLF.
set(BM_INI_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/ini_default.h")
add_custom_command(
    OUTPUT "${BM_INI_HEADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND ${CMAKE_COMMAND} "-DIN=${CMAKE_CURRENT_SOURCE_DIR}/SpeederBike.ini"
                             "-DOUT=${BM_INI_HEADER}"
                             -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_ini.cmake"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/SpeederBike.ini"
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_ini.cmake"
    COMMENT "Embedding SpeederBike.ini"
    VERBATIM
)

add_library(SpeederBike SHARED
    "${BM_INI_HEADER}"
    src/dllmain.cpp
    src/core/mod.cpp
    src/core/log.cpp
    src/core/paths.cpp
    src/game/mem.cpp
    src/game/hash.cpp
    src/game/farhook.cpp
    src/game/gamefile.cpp
    src/game/broomy.cpp
    src/game/broomrows.cpp
    src/game/broomchart.cpp
    src/game/tablefile.cpp
    src/game/lz4.cpp
    src/game/grant.cpp
    src/game/analogspeed.cpp
    src/game/equipfix.cpp
    src/game/riderfix.cpp
    src/game/aimrate.cpp
    src/game/speedersound.cpp
    src/resources.rc
    src/speeder.rc
)

# SpeederBike.ini again, this time as the INIMETA resource (src/resources.rc);
# rebuild when the ini's ;@ directives or defaults change.
set_source_files_properties(src/resources.rc PROPERTIES OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/SpeederBike.ini")
# The speeder's files (src/speeder.rc, from speeder/pack_assets.py).
file(GLOB_RECURSE BM_SPEEDER_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
set_source_files_properties(src/speeder.rc PROPERTIES OBJECT_DEPENDS "${BM_SPEEDER_ASSETS}")

target_include_directories(SpeederBike PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated" ${minhook_SOURCE_DIR}/src/hde)
target_link_libraries(SpeederBike PRIVATE minhook)
target_compile_definitions(SpeederBike PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_compile_options(SpeederBike PRIVATE /utf-8 /W3)

set_target_properties(SpeederBike PROPERTIES PREFIX "" SUFFIX ".asi" OUTPUT_NAME "SpeederBike")

if(MSVC)
    target_compile_options(SpeederBike PRIVATE /guard:cf)
    target_link_options(SpeederBike PRIVATE /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT /guard:cf)
endif()

# The speeder's tables and package list on top of Broomy's, with Broomy's
# own builders from its source, built apart so they see only Broomy's
# headers: build.bat stackcheck IN.
set(BROOMY_SRC "${CMAKE_SOURCE_DIR}/../../Broomy/mod/src" CACHE PATH "Broomy's mod/src, for stackcheck")
add_library(broomyref STATIC EXCLUDE_FROM_ALL
    "${BROOMY_SRC}/game/broomrows.cpp"
    "${BROOMY_SRC}/game/broomchart.cpp"
)
target_include_directories(broomyref PRIVATE "${BROOMY_SRC}")
target_compile_definitions(broomyref PRIVATE broomrows=broomyrows broomchart=broomychart _CRT_SECURE_NO_WARNINGS)
target_compile_options(broomyref PRIVATE /utf-8 /W3)
add_executable(stackcheck EXCLUDE_FROM_ALL
    tests/stackcheck.cpp
    src/game/broomrows.cpp
    src/game/broomchart.cpp
    src/game/tablefile.cpp
    src/game/lz4.cpp
    src/game/hash.cpp
)
target_link_libraries(stackcheck PRIVATE broomyref)
target_include_directories(stackcheck PRIVATE src)
target_compile_definitions(stackcheck PRIVATE _CRT_SECURE_NO_WARNINGS
    "BROOMY_ROWS_H=\"${BROOMY_SRC}/game/broomrows.h\""
    "BROOMY_CHART_H=\"${BROOMY_SRC}/game/broomchart.h\"")
target_compile_options(stackcheck PRIVATE /utf-8 /W3)

# The offline check of Broomy's charts: build.bat chartcheck IN OUT.
add_executable(chartcheck EXCLUDE_FROM_ALL
    tests/chartcheck.cpp
    src/game/broomchart.cpp
)
target_include_directories(chartcheck PRIVATE src)
target_compile_definitions(chartcheck PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(chartcheck PRIVATE /utf-8 /W3)

# The offline check of the speeder's engine sounds: build.bat soundcheck.
add_executable(soundcheck EXCLUDE_FROM_ALL
    tests/soundcheck.cpp
    src/game/speedersound.cpp
    src/speeder.rc
)
target_include_directories(soundcheck PRIVATE src)
target_compile_definitions(soundcheck PRIVATE SPEEDER_SOUNDCHECK _CRT_SECURE_NO_WARNINGS)
target_compile_options(soundcheck PRIVATE /utf-8 /W3)

add_custom_command(TARGET SpeederBike POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/dist"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:SpeederBike>" "${CMAKE_SOURCE_DIR}/dist/"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/SpeederBike.ini" "${CMAKE_SOURCE_DIR}/dist/"
    COMMENT "Staging dist/"
    VERBATIM
)
