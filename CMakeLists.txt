cmake_minimum_required(VERSION 3.0)
project(java-tools)

set(CMAKE_MACOSX_RPATH 1)

find_package(PkgConfig)
pkg_check_modules(GLIB2 glib-2.0)
pkg_check_modules(LIBZIP libzip)
pkg_check_modules(SQLITE sqlite3)

set(CMAKE_C_FLAGS "-std=c99 -pedantic -Wall -D_POSIX_SOURCE")
include_directories(
    /usr/local/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${GLIB2_INCLUDE_DIRS}
    ${LIBZIP_INCLUDE_DIRS}
    ${SQLITE_INCLUDE_DIRS}
)

link_directories(
    /usr/local/lib
    ${GLIB2_LIBRARY_DIRS}
    ${LIBZIP_LIBRARY_DIRS}
    ${SQLITE_LIBRARY_DIRS}
)

add_executable(java-dumpclass src/dumpclass.c)
target_link_libraries(java-dumpclass classreader ${GLIB2_LIBRARIES})

add_executable(java-indexproject src/indexproject.c)
target_link_libraries(java-indexproject classreader ${GLIB2_LIBRARIES} ${LIBZIP_LIBRARIES} ${SQLITE_LIBRARIES})

add_executable(java-findjar src/findjar.c)
target_link_libraries(java-findjar classreader ${GLIB2_LIBRARIES} ${LIBZIP_LIBRARIES})

install(TARGETS
    java-dumpclass
    java-indexproject
    java-findjar
    DESTINATION
    bin
)
