cmake_minimum_required(VERSION 3.10)

project(VanillaMinimapTracking LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

add_subdirectory(external/minhook)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "src/*.h")

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

# Tag-driven version: the release workflow passes `-DVANILLAMINIMAPTRACKING_TAG=v1.2.3`
# which gets packed into a single int (1*10000 + 2*100 + 3 = 10203) as
# `VANILLAMINIMAPTRACKING_VERSION_VALUE`. Dev builds with no tag default to 1.
set(VANILLAMINIMAPTRACKING_VERSION_VALUE 1)

set(VANILLAMINIMAPTRACKING_TAG
    ""
    CACHE STRING "Release tag like v1.2.3")

if(VANILLAMINIMAPTRACKING_TAG MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
  set(_MAJOR "${CMAKE_MATCH_1}")
  set(_MINOR "${CMAKE_MATCH_2}")
  set(_PATCH "${CMAKE_MATCH_3}")
  math(EXPR VANILLAMINIMAPTRACKING_VERSION_VALUE
       "${_MAJOR} * 10000 + ${_MINOR} * 100 + ${_PATCH}")
elseif(VANILLAMINIMAPTRACKING_TAG)
  message(
    FATAL_ERROR
      "Invalid VANILLAMINIMAPTRACKING_TAG: ${VANILLAMINIMAPTRACKING_TAG} (expected vMAJOR.MINOR.PATCH)"
  )
endif()

target_compile_definitions(
  ${PROJECT_NAME}
  PRIVATE VANILLAMINIMAPTRACKING_VERSION_VALUE=${VANILLAMINIMAPTRACKING_VERSION_VALUE})

target_link_libraries(${PROJECT_NAME} PRIVATE minhook)

target_include_directories(
  ${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
                          external/minhook/include)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "VanillaMinimapTracking")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MT")
set(CMAKE_CXX_FLAGS_RELEASE "/MT /O2 /Ob2 /DNDEBUG")
set(CMAKE_SHARED_LINKER_FLAGS
    "/OPT:REF /OPT:ICF /MERGE:.rdata=.text /MERGE:.pdata=.text /SECTION:.text,EWR /NODEFAULTLIB:MSVCRT"
)
