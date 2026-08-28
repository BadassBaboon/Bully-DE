cmake_minimum_required(VERSION 3.20)
project(Bully-DE LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Link the CRT statically. Without this the .asi imports MSVCP140.dll and
# VCRUNTIME140.dll, and anyone without the VC++ redistributable gets a silent
# load failure with no log to explain it.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Bully: Scholarship Edition is a 32-bit game. Configure with -A Win32.")
endif()

add_library(${PROJECT_NAME} SHARED
    src/Main.cpp
    src/Config.cpp
    src/Config.h
    src/Logger.cpp
    src/Logger.h
    src/Memory.cpp
    src/Memory.h
    src/Patch.cpp
    src/Patch.h
    src/UnpackHook.cpp
    src/UnpackHook.h
    src/features/ShadowFix.cpp
    src/features/ShadowFix.h
    src/features/BloomFix.cpp
    src/features/BloomFix.h
    src/features/DrawDistanceFix.cpp
    src/features/DrawDistanceFix.h
    src/features/AAFix.cpp
    src/features/AAFix.h
    src/features/CoronaPatches.cpp
    src/features/CoronaPatches.h
    src/features/GraphicsFix.cpp
    src/features/GraphicsFix.h
    src/features/Diagnostics.cpp
    src/features/Diagnostics.h
)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "Bully-DE"
    SUFFIX ".asi"
    PREFIX ""
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /MP /permissive- /EHsc)
    target_link_options(${PROJECT_NAME} PRIVATE /INCREMENTAL:NO /OPT:REF /OPT:ICF)
endif()

# Put the INI next to the built .asi so the build folder is directly installable.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Bully-DE.ini"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Bully-DE.ini"
)
