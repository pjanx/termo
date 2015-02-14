project (termo C)
cmake_minimum_required (VERSION 2.8.5)

if ("${CMAKE_C_COMPILER_ID}" MATCHES "GNU" OR CMAKE_COMPILER_IS_GNUC)
	set (CMAKE_C_FLAGS "-std=c99")
	set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wall -Wextra")
endif ("${CMAKE_C_COMPILER_ID}" MATCHES "GNU" OR CMAKE_COMPILER_IS_GNUC)

# Version
set (project_VERSION_MAJOR 0)
set (project_VERSION_MINOR 1)
set (project_VERSION_PATCH 0)

set (project_VERSION ${project_VERSION_MAJOR})
set (project_VERSION ${project_VERSION}.${project_VERSION_MINOR})
set (project_VERSION ${project_VERSION}.${project_VERSION_PATCH})

set (project_API_VERSION ${project_VERSION_MAJOR})

# Names
set (project_LIB_NAME "termo-${project_API_VERSION}")
set (project_INCLUDE_NAME "termo-${project_API_VERSION}")
set (project_CMAKE_NAME "Termo")

# Dependecies
find_package (Curses)
find_package (PkgConfig REQUIRED)
pkg_check_modules (glib glib-2.0 gio-2.0)
pkg_check_modules (ncursesw ncursesw)
pkg_check_modules (unibilium unibilium>=0.1.0)

# Header files with configuration
configure_file (${PROJECT_SOURCE_DIR}/termo-config.h.in
	${PROJECT_BINARY_DIR}/termo-config.h)
include_directories (${PROJECT_SOURCE_DIR} ${PROJECT_BINARY_DIR})

# Project source files
set (lib_sources
	termo.c
	driver-csi.c
	driver-ti.c)
set (lib_headers
	termo.h
	termo-internal.h
	${PROJECT_BINARY_DIR}/termo-config.h)

# Project libraries
if (unibilium_FOUND)
	include_directories (${unibilium_INCLUDE_DIRS})
	set (lib_libraries ${unibilium_LIBRARIES})
	add_definitions (-DHAVE_UNIBILIUM)
elseif (ncursesw_FOUND)
	include_directories (${ncursesw_INCLUDE_DIRS})
	set (lib_libraries ${ncursesw_LIBRARIES})
elseif (CURSES_FOUND)
	include_directories (${CURSES_INCLUDE_DIR})
	set (lib_libraries ${CURSES_LIBRARY})
else (CURSES_FOUND)
	message (SEND_ERROR "Unibilium not found, Curses not found")
endif (unibilium_FOUND)

# Create the library targets
add_library (termo SHARED ${lib_sources} ${lib_headers})
target_link_libraries (termo ${lib_libraries})
set_target_properties (termo PROPERTIES
	OUTPUT_NAME ${project_LIB_NAME}
	VERSION ${project_VERSION}
	SOVERSION ${project_API_VERSION})

add_library (termo-static STATIC ${lib_sources} ${lib_headers})
target_link_libraries (termo-static ${lib_libraries})
set_target_properties (termo-static PROPERTIES
	OUTPUT_NAME ${project_LIB_NAME}
	VERSION ${project_VERSION}
	SOVERSION ${project_API_VERSION})

# A fix for: relocation R_X86_64_32 against `a local symbol' can not be
#   used when making a shared object; recompile with -fPIC
# See http://www.cmake.org/pipermail/cmake/2007-May/014350.html
# This should enable linking the static library into a shared one.
if (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
	set_target_properties (termo-static PROPERTIES COMPILE_FLAGS "-fPIC")
endif (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")

# Demos
add_executable (demo-async EXCLUDE_FROM_ALL demo-async.c)
target_link_libraries (demo-async termo-static ${lib_libraries})

add_executable (demo-draw EXCLUDE_FROM_ALL demo-draw.c)
target_link_libraries (demo-draw termo-static ${lib_libraries})

add_executable (demo EXCLUDE_FROM_ALL demo.c)
target_link_libraries (demo termo-static ${lib_libraries})

set (demos demo demo-async demo-draw)
if (glib_FOUND)
	include_directories (${glib_INCLUDE_DIRS})
	add_executable (demo-glib EXCLUDE_FROM_ALL demo-glib.c)
	target_link_libraries (demo-glib
		termo-static ${lib_libraries} ${glib_LIBRARIES})
	list (APPEND demos demo-glib)
endif (glib_FOUND)

add_custom_target (demos DEPENDS ${demos})

# The files to be installed
include (GNUInstallDirs)
install (TARGETS termo termo-static DESTINATION ${CMAKE_INSTALL_LIBDIR})
install (FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
install (FILES termo.h ${PROJECT_BINARY_DIR}/termo-config.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${project_INCLUDE_NAME})

# Configuration for other CMake projects
configure_file (config.cmake.in
	${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config.cmake @ONLY)
configure_file (config-version.cmake.in
	${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake @ONLY)

install (FILES ${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config.cmake 
	${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}-${project_VERSION})

# Do some unit tests
option (BUILD_TESTING "Build tests" OFF)
# TODO: glob, port the tests to CTest
set (project_tests)

if (BUILD_TESTING)
	enable_testing ()
	set (test_common_sources t/taplib.c t/taplib.h)

	foreach (name ${project_tests})
		add_executable (test-${name} t/${name}.c ${test_common_sources})
		target_link_libraries (test-${name} ${lib_libraries})
		add_test (test-${name} test-${name})
	endforeach (name)
endif (BUILD_TESTING)

# pkg-config
file (WRITE "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.pc"
	"Name: ${PROJECT_NAME}\n"
	"Description: Terminal key input library\n"
	"Version: ${project_VERSION}\n"
	"Libs: -L${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR} -l${project_LIB_NAME}\n"
	"Libs.private: ${lib_libraries}\n"
	"Cflags: -I${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}/${project_INCLUDE_NAME}\n")
install (FILES "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.pc"
	DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")

# CPack
set (CPACK_PACKAGE_DESCRIPTION_SUMMARY "Terminal key input library")
set (CPACK_PACKAGE_VENDOR "Premysl Janouch")
set (CPACK_PACKAGE_CONTACT "Přemysl Janouch <p.janouch@gmail.com>")
set (CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set (CPACK_PACKAGE_VERSION_MAJOR ${project_VERSION_MAJOR})
set (CPACK_PACKAGE_VERSION_MINOR ${project_VERSION_MINOR})
set (CPACK_PACKAGE_VERSION_PATCH ${project_VERSION_PATCH})
set (CPACK_GENERATOR "TGZ;ZIP")
set (CPACK_PACKAGE_FILE_NAME
	"${CMAKE_PROJECT_NAME}-${project_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set (CPACK_PACKAGE_INSTALL_DIRECTORY "${CMAKE_PROJECT_NAME}-${project_VERSION}")
set (CPACK_SOURCE_GENERATOR "TGZ;ZIP")
set (CPACK_SOURCE_IGNORE_FILES "/\\\\.git;/build;/CMakeLists.txt.user")
set (CPACK_SOURCE_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${project_VERSION}")

include (CPack)
