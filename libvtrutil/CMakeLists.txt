cmake_minimum_required(VERSION 2.8.12)

project("libvtrutil")

#Version info
set(VTR_VERSION_FILE_IN ${CMAKE_CURRENT_SOURCE_DIR}/src/vtr_version.cpp.in)
set(VTR_VERSION_FILE_OUT ${CMAKE_CURRENT_BINARY_DIR}/vtr_version.cpp)
set(VTR_COMPILER_INFO "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} on ${CMAKE_SYSTEM} ${CMAKE_SYSTEM_PROCESSOR}")

#Set default version numbers in case not specified
if(NOT DEFINED VTR_VERSION_MAJOR)
    set(VTR_VERSION_MAJOR 0)
endif()

if(NOT DEFINED VTR_VERSION_MINOR)
    set(VTR_VERSION_MINOR 0)
endif()

if(NOT DEFINED VTR_VERSION_PATCH)
    set(VTR_VERSION_PATCH 0)
endif()

# We always update the vtr_version.cpp file every time the project is built, 
# to ensure the git revision and dirty status are up to date.
#
# We need to do this in two stages:
#
# 1) We a custom target 'version' (which is always out of date) so it will always be run.
#    It touches the unprocessed version input file so it too will always be out of date.
#
# 2) The custom command depends on the touched version input file and generates the processed 
#    version file, with updated values. The custom command uses the configure_version.cmake 
#    script to generate the up-to-date vtr_version.cpp
add_custom_target(version ALL
    COMMAND ${CMAKE_COMMAND} -E touch ${VTR_VERSION_FILE_IN})

add_custom_command(OUTPUT ${VTR_VERSION_FILE_OUT}
    COMMAND ${CMAKE_COMMAND} 
                    -D IN_FILE=${VTR_VERSION_FILE_IN}
                    -D OUT_FILE=${VTR_VERSION_FILE_OUT}
                    -D VTR_VERSION_MAJOR=${VTR_VERSION_MAJOR}
                    -D VTR_VERSION_MINOR=${VTR_VERSION_MINOR}
                    -D VTR_VERSION_PATCH=${VTR_VERSION_PATCH}
                    -D VTR_VERSION_PRERELEASE=${VTR_VERSION_PRERELEASE}
                    -D VTR_COMPILER_INFO=${VTR_COMPILER_INFO}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules/configure_version.cmake
    MAIN_DEPENDENCY ${VTR_VERSION_FILE_IN}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	VERBATIM)


#
# Source files and library
#
file(GLOB_RECURSE LIB_SOURCES src/*.cpp)
file(GLOB_RECURSE LIB_HEADERS src/*.hpp src/*.h)
files_to_dirs(LIB_HEADERS LIB_INCLUDE_DIRS)

#Add the version file to the sources
list(APPEND LIB_SOURCES ${VTR_VERSION_FILE_OUT})

#Create the library
add_library(libvtrutil STATIC
             ${LIB_HEADERS}
             ${LIB_SOURCES})
target_include_directories(libvtrutil PUBLIC ${LIB_INCLUDE_DIRS})
set_target_properties(libvtrutil PROPERTIES PREFIX "") #Avoid extra 'lib' prefix

#Ensure version is always up to date by requiring version to be run first
add_dependencies(libvtrutil version)

#Specify link-time dependancies
target_link_libraries(libvtrutil
                        liblog)

install(TARGETS libvtrutil DESTINATION bin)
