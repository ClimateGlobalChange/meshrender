cmake_minimum_required(VERSION 3.10)
project(meshrender)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(/usr/local/include)
link_directories(/usr/local/lib)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/CMake ${CMAKE_MODULE_PATH})

# Set default build type if not already defined
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(WIN32)
  add_compile_definitions(_USE_MATH_DEFINES 1)
endif()

# Required dependencies
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(GLEW REQUIRED)
find_package(NetCDF REQUIRED)

# Output directories for out-of-source builds
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_subdirectory(src)

# Ensure the runtime output directory exists.
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Install rule: copy all files from the build's bin directory to the install prefix's bin directory.
install(DIRECTORY "${CMAKE_BINARY_DIR}/bin/" DESTINATION bin)
