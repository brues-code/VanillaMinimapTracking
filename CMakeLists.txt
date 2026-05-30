cmake_minimum_required(VERSION 3.10)

project(VanillaMinimapTracking LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

add_subdirectory(external/minhook)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "src/*.h")

# Tag-driven version: the release workflow passes `-DVANILLAMINIMAPTRACKING_TAG=v1.2.3`
# which gets packed into a single int (1*10000 + 2*100 + 3 = 10203) as
# `VANILLAMINIMAPTRACKING_VERSION_VALUE`. Dev builds default to v9999.99.99
# ("newer than every plausible release") so they always win the
# disk-vs-embedded version comparison in Embedded.cpp.
set(VANILLAMINIMAPTRACKING_VERSION_VALUE 99999999)
set(VANILLAMINIMAPTRACKING_VERSION_STRING "9999.99.99")

set(VANILLAMINIMAPTRACKING_TAG
    ""
    CACHE STRING "Release tag like v1.2.3")

if(VANILLAMINIMAPTRACKING_TAG MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
  set(_MAJOR "${CMAKE_MATCH_1}")
  set(_MINOR "${CMAKE_MATCH_2}")
  set(_PATCH "${CMAKE_MATCH_3}")
  math(EXPR VANILLAMINIMAPTRACKING_VERSION_VALUE
       "${_MAJOR} * 10000 + ${_MINOR} * 100 + ${_PATCH}")
  set(VANILLAMINIMAPTRACKING_VERSION_STRING "${_MAJOR}.${_MINOR}.${_PATCH}")
elseif(VANILLAMINIMAPTRACKING_TAG)
  message(
    FATAL_ERROR
      "Invalid VANILLAMINIMAPTRACKING_TAG: ${VANILLAMINIMAPTRACKING_TAG} (expected vMAJOR.MINOR.PATCH)"
  )
endif()

# Bundle `MinimapBlips/` into a generated header so the DLL can surface the
# addon to the engine even when the user doesn't have it on disk. The
# file-read hook in `src/addons/Embedded.cpp` serves these buffers when the
# engine asks for paths under `Interface\AddOns\MinimapBlips\`. embed_addon
# also stamps the addon's TOC `## Version:` line from the build tag so the
# embedded version always matches the DLL version.
set(EMBEDDED_ADDON_SRC "${CMAKE_CURRENT_SOURCE_DIR}/MinimapBlips")
set(EMBEDDED_ADDON_HEADER "${CMAKE_BINARY_DIR}/generated/embedded_minimapblips.h")
file(GLOB_RECURSE EMBEDDED_ADDON_FILES CONFIGURE_DEPENDS "${EMBEDDED_ADDON_SRC}/*")
add_custom_command(
    OUTPUT "${EMBEDDED_ADDON_HEADER}"
    COMMAND ${CMAKE_COMMAND}
        -DSRC_DIR=${EMBEDDED_ADDON_SRC}
        -DOUT_HEADER=${EMBEDDED_ADDON_HEADER}
        -DADDON_NAME=MinimapBlips
        -DSTAMP_TOC=MinimapBlips.toc
        -DSTAMP_VERSION=${VANILLAMINIMAPTRACKING_VERSION_STRING}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_addon.cmake"
    DEPENDS
        ${EMBEDDED_ADDON_FILES}
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_addon.cmake"
    COMMENT "Embedding MinimapBlips addon into ${EMBEDDED_ADDON_HEADER}"
    VERBATIM)

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS} ${EMBEDDED_ADDON_HEADER})

target_compile_definitions(
  ${PROJECT_NAME}
  PRIVATE VANILLAMINIMAPTRACKING_VERSION_VALUE=${VANILLAMINIMAPTRACKING_VERSION_VALUE})

target_link_libraries(${PROJECT_NAME} PRIVATE minhook)

target_include_directories(
  ${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
                          ${CMAKE_BINARY_DIR}/generated
                          external/minhook/include)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "VanillaMinimapTracking")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MT")
set(CMAKE_CXX_FLAGS_RELEASE "/MT /O2 /Ob2 /DNDEBUG")
set(CMAKE_SHARED_LINKER_FLAGS
    "/OPT:REF /OPT:ICF /MERGE:.rdata=.text /MERGE:.pdata=.text /SECTION:.text,EWR /NODEFAULTLIB:MSVCRT"
)
