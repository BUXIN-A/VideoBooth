cmake_minimum_required(VERSION 3.20)

project(VideoBooth VERSION 1.1.0 LANGUAGES CXX)

if(NOT MSVC)
    message(FATAL_ERROR "VideoBooth 当前仅支持 MSVC 工具链构建")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# 统一输出目录：assets 与 exe 同级，便于按打包结构直接运行
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${cfg}" cfg_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_upper} "${CMAKE_BINARY_DIR}/bin")
endforeach()

file(GLOB_RECURSE VB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/*.h")

add_executable(VideoBooth WIN32
    ${VB_SOURCES}
    "${CMAKE_SOURCE_DIR}/resources/app.rc")

set_target_properties(VideoBooth PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:VideoBooth>")

target_include_directories(VideoBooth PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/resources")

target_compile_definitions(VideoBooth PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00)

target_compile_options(VideoBooth PRIVATE
    /W4 /permissive- /utf-8 /MP /EHsc
    /external:anglebrackets /external:W0)

# manifest 由 resources/app.rc 内嵌，禁止链接器再生成一份
target_link_options(VideoBooth PRIVATE /MANIFEST:NO)

target_link_libraries(VideoBooth PRIVATE
    opengl32
    mfplat
    mfreadwrite
    mfuuid
    mf
    ole32
    oleaut32
    windowscodecs
    shlwapi
    shell32
    dwmapi
    user32
    gdi32
    advapi32
    winhttp
    winmm)

# 运行期资源：assets 与 exe 保持同级
add_custom_command(TARGET VideoBooth POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:VideoBooth>/assets"
    COMMENT "复制 assets 资源目录到输出目录")

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT VideoBooth)
