cmake_minimum_required (VERSION 3.7.1)

project (TCEngine C)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

include_directories(
	"${CMAKE_SOURCE_DIR}/src"
	"${CMAKE_SOURCE_DIR}/include"
)

include_directories(deps/)

# Add libuv as static lib
include_directories(deps/libuv/include)
add_subdirectory(deps/libuv EXCLUDE_FROM_ALL)
set_target_properties(uv_a PROPERTIES COMPILE_FLAGS "-fPIC")

# Add glfw as static lib
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_USE_WAYLAND ${USE_WAYLAND} CACHE BOOL "" FORCE)
add_subdirectory(deps/glfw)
include_directories(deps/glfw/include)

# Add openal as static lib
set(ALSOFT_UTILS OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/openal)
include_directories(deps/openal/include)

# Add luajit as static lib
include(deps/luajit.cmake)
include_directories(deps/luajit/src)
set(LUAJIT luajit-5.1)

# Add GLAD
add_library(GLAD STATIC deps/glad/include/glad/glad.h deps/glad/src/glad.c)
target_include_directories(GLAD PRIVATE deps/glad/include)
include_directories(deps/glad/include)

# Add libcoro as static lib
set(ENABLE_BUNDLED_LIBCORO ON)
include(deps/libcoro.cmake)
libcoro_build()
include_directories(deps/libcoro)

include_directories(deps/deboost.context/include)
add_subdirectory(deps/deboost.context)

file(GLOB tc_sources src/*)
add_library(tc_static STATIC ${tc_sources})

# Put all libraries in LIBS
set(LIBS coro fcontext uv_a opengl32 glfw OpenAL GLAD tc_static "${CMAKE_DL_LIBS}")

add_executable(${PROJECT_NAME} main.c)

# Build luajit as dll on windows
if(WIN32)
    add_definitions(-DLUA_BUILD_AS_DLL -DLUA_LIB)
endif()
# Link with libraries
if(WIN32)
    target_link_libraries(${PROJECT_NAME} ${LUAJIT} ${LIBS})
elseif("${CMAKE_SYSTEM_NAME}" MATCHES "Linux")
    target_link_libraries(${PROJECT_NAME} ${LIBS} rt)
else()
    target_link_libraries(${PROJECT_NAME} ${LIBS})
endif()