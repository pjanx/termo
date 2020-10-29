cmake_minimum_required (VERSION 3.0)
project (termo VERSION 0.1.0 LANGUAGES C)

if ("${CMAKE_C_COMPILER_ID}" MATCHES "GNU" OR CMAKE_COMPILER_IS_GNUCC)
	set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")
	set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wall -Wextra")
endif ()

# Version
set (project_API_VERSION ${PROJECT_VERSION_MAJOR})

# Names
set (project_LIB_NAME "termo-${project_API_VERSION}")
set (project_INCLUDE_NAME "termo-${project_API_VERSION}")
set (project_CMAKE_NAME "Termo")

# For custom modules
set (CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

# Dependecies
find_package (Curses)
find_package (PkgConfig REQUIRED)
find_package (Ncursesw)
pkg_check_modules (glib glib-2.0 gio-2.0)
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
# We need ncurses for one of the demos, so we're always looking
if (Ncursesw_FOUND)
	include_directories (${Ncursesw_INCLUDE_DIRS})
	set (curses_libraries ${Ncursesw_LIBRARIES})
elseif (CURSES_FOUND)
	include_directories (${CURSES_INCLUDE_DIR})
	set (curses_libraries ${CURSES_LIBRARY})
endif ()

if (unibilium_FOUND)
	include_directories (${unibilium_INCLUDE_DIRS})
	set (lib_libraries ${unibilium_LIBRARIES})
	add_definitions (-DHAVE_UNIBILIUM)
elseif (curses_libraries)
	include_directories (${Ncursesw_INCLUDE_DIRS})
	set (lib_libraries ${curses_libraries})
else ()
	message (SEND_ERROR "Unibilium not found, Curses not found")
endif ()

# -liconv may or may not be a part of libc
find_library (iconv_LIBRARIES iconv)
if (iconv_LIBRARIES)
	list (APPEND lib_libraries ${iconv_LIBRARIES})
endif ()

# Create the library targets
add_library (termo SHARED ${lib_sources} ${lib_headers})
target_link_libraries (termo ${lib_libraries})
set_target_properties (termo PROPERTIES
	OUTPUT_NAME ${project_LIB_NAME}
	VERSION ${PROJECT_VERSION}
	SOVERSION ${project_API_VERSION})

add_library (termo-static STATIC ${lib_sources} ${lib_headers})
target_link_libraries (termo-static ${lib_libraries})
set_target_properties (termo-static PROPERTIES
	OUTPUT_NAME ${project_LIB_NAME}
	VERSION ${PROJECT_VERSION}
	SOVERSION ${project_API_VERSION})

# A fix for: relocation R_X86_64_32 against `a local symbol' can not be
#   used when making a shared object; recompile with -fPIC
# See http://www.cmake.org/pipermail/cmake/2007-May/014350.html
# This should enable linking the static library into a shared one.
if (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
	set_target_properties (termo-static PROPERTIES COMPILE_FLAGS "-fPIC")
endif ()

# Demos
add_executable (demo-async EXCLUDE_FROM_ALL demo-async.c)
target_link_libraries (demo-async termo-static ${lib_libraries})

add_executable (demo-draw EXCLUDE_FROM_ALL demo-draw.c)
target_link_libraries (demo-draw termo-static ${lib_libraries} ${curses_libraries})

add_executable (demo EXCLUDE_FROM_ALL demo.c)
target_link_libraries (demo termo-static ${lib_libraries})

set (demos demo demo-async demo-draw)
if (glib_FOUND)
	include_directories (${glib_INCLUDE_DIRS})
	add_executable (demo-glib EXCLUDE_FROM_ALL demo-glib.c)
	target_link_libraries (demo-glib
		termo-static ${lib_libraries} ${glib_LIBRARIES})
	list (APPEND demos demo-glib)
endif ()

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
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}-${PROJECT_VERSION})

# Do some unit tests
option (BUILD_TESTING "Build tests" OFF)
# TODO: port the tests to CTest (?)
set (project_tests
	01base
	02getkey
	03utf8
	04flags
	05read
	06buffer
	10keyname
	11strfkey
	12strpkey
	13cmpkey
	20canon
	30mouse
	31position
	32modereport
	33focus
	39csi)

if (BUILD_TESTING)
	enable_testing ()
	set (test_common_sources tests/taplib.c tests/taplib.h)

	foreach (name ${project_tests})
		add_executable (test-${name} tests/${name}.c ${test_common_sources})
		target_link_libraries (test-${name} termo-static ${lib_libraries})
		add_test (NAME ${PROJECT_NAME}.${name} COMMAND test-${name})
	endforeach ()
endif ()

# pkg-config
file (WRITE "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.pc"
	"Name: ${PROJECT_NAME}\n"
	"Description: Terminal key input library\n"
	"Version: ${PROJECT_VERSION}\n"
	"Libs: -L${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR} -l${project_LIB_NAME}\n"
	"Libs.private: ${lib_libraries}\n"
	"Cflags: -I${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}/${project_INCLUDE_NAME}\n")
install (FILES "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.pc"
	DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")

# CPack
set (CPACK_PACKAGE_DESCRIPTION_SUMMARY "Terminal key input library")
set (CPACK_PACKAGE_VENDOR "Premysl Eric Janouch")
set (CPACK_PACKAGE_CONTACT "Přemysl Eric Janouch <p@janouch.name>")
set (CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set (CPACK_GENERATOR "TGZ;ZIP")
set (CPACK_PACKAGE_FILE_NAME
	"${CMAKE_PROJECT_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set (CPACK_PACKAGE_INSTALL_DIRECTORY "${CMAKE_PROJECT_NAME}-${PROJECT_VERSION}")
set (CPACK_SOURCE_GENERATOR "TGZ;ZIP")
set (CPACK_SOURCE_IGNORE_FILES "/\\\\.git;/build;/CMakeLists.txt.user")
set (CPACK_SOURCE_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${PROJECT_VERSION}")

include (CPack)
