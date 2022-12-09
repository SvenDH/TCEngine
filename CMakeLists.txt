cmake_minimum_required (VERSION 3.7.1)

project(TCEngine C)

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

find_package(Vulkan)

if (NOT ${Vulkan_INCLUDE_DIRS} STREQUAL "")
	set(VULKAN_PATH ${Vulkan_INCLUDE_DIRS})
	STRING(REGEX REPLACE "/Include" "" VULKAN_PATH ${VULKAN_PATH})
endif()
		 
if(NOT Vulkan_FOUND)
	message(STATUS "Failed to locate Vulkan SDK, retrying again...")
	if(EXISTS "${VULKAN_PATH}")
		message(STATUS "Successfully located the Vulkan SDK: ${VULKAN_PATH}")
	else()
		message("Error: Unable to locate Vulkan SDK.")
		return()
	endif()
endif()

add_definitions(-DVK_USE_PLATFORM_WIN32_KHR)

set(VULKAN_LIB_LIST "vulkan-1")

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	# Include Vulkan header files from Vulkan SDK
	include_directories(AFTER ${VULKAN_PATH}/Include)

	# Link directory for vulkan-1
	link_directories(${VULKAN_PATH}/Bin;${VULKAN_PATH}/Lib;)
endif()

# Add glfw as static lib
include_directories($ENV{VULKAN_SDK_PATH}/Include)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_USE_WAYLAND ${USE_WAYLAND} CACHE BOOL "" FORCE)
add_subdirectory(deps/glfw)
include_directories(deps/glfw/include)

# Add openal as static lib
#set(ALSOFT_UTILS OFF CACHE BOOL "" FORCE)
#add_subdirectory(deps/openal)
#include_directories(deps/openal/include)

# Add luajit as static lib
include(deps/luajit.cmake)
include_directories(deps/luajit/src)
set(LUAJIT luajit-5.1)

# Add GLAD
add_library(GLAD STATIC deps/glad/include/glad/glad.h deps/glad/src/glad.c)
target_include_directories(GLAD PRIVATE deps/glad/include)
include_directories(deps/glad/include)

# Add boost.context as static lib
include_directories(deps/deboost.context/include)
add_subdirectory(deps/deboost.context)

# Add zlib
include_directories(ZLIB deps/zlib)
find_package(ZLIB)

# Add minizip
set(MZ_ZSTD OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/minizip)
include_directories(deps/minizip)

file(GLOB tc_sources include/* src/* src/datastructures/*)
add_library(tc_static STATIC ${tc_sources})

# Put all libraries in LIBS
set(LIBS fcontext ZLIB::ZLIB minizip uv_a glfw ${VULKAN_LIB_LIST} GLAD tc_static "${CMAKE_DL_LIBS}")

add_executable(${PROJECT_NAME} main.c)

target_compile_definitions(${PROJECT_NAME} PRIVATE VK_USE_PLATFORM_WIN32_KHR)

# Build luajit as dll on windows
if(WIN32)
    add_definitions(-DLUA_BUILD_AS_DLL -DLUA_LIB)
endif()
# Link with libraries
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${LUAJIT} ${LIBS})
elseif("${CMAKE_SYSTEM_NAME}" MATCHES "Linux")
    target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBS} rt)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBS})
endif()