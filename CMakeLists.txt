cmake_minimum_required(VERSION 2.8)

set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake" ${CMAKE_MODULE_PATH})

add_subdirectory("td/")
include_directories ("td/")
include_directories ("td/tdactor/")
include_directories ("td/tdutils/")
include_directories ("td/tdtl/")
include_directories ("td/td/generate/")
include_directories ("td/td/generate/default_td_api/")
include_directories($<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>/td/tdutils)
link_directories ("/opt/local/lib/")

find_package(Lua REQUIRED)
include_directories(${LUA_INCLUDE_DIR})

include (CheckIncludeFiles)
check_include_files (execinfo.h HAVE_EXECINFO_H)
if (HAVE_EXECINFO_H) 
  add_definitions("-DHAVE_EXECINFO_H=1")
endif (HAVE_EXECINFO_H)

check_include_files (pwd.h HAVE_PWD_H)
if (HAVE_PWD_H) 
  add_definitions("-DHAVE_PWD_H=1")
endif (HAVE_PWD_H)

include_directories (${OPENSSL_INCLUDE_DIR})

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wconversion -Wno-sign-conversion -std=c++14 -fno-omit-frame-pointer")
set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -std=c++14")

set (TDBOT_SOURCE
  main.cpp
  cliclient.cpp
  clilua.cpp
)


add_executable (telegram-bot ${TDBOT_SOURCE} ${TL_TD_JSON_AUTO} )

set_source_files_properties(${TL_TD_JSON_AUTO} PROPERTIES GENERATED TRUE)
add_dependencies(telegram-bot tl_generate_json)
target_link_libraries (telegram-bot tdclient ${ZLIB_LIBRARIES} -lconfig++ ${LUA_LIBRARIES} -lpthread -lcrypto -lssl )
#target_link_libraries (telegram-curses tdc tdclient ${OPENSSL_LIBRARIES}
#  ${ZLIB_LIBRARIES} ${LIBCONFIG_LIBRARY} ${LIBEVENT2_LIBRARY}
#  ${LIBEVENT1_LIBRARY} ${LIBJANSSON_LIBRARY} ${LUA_LIBRARIES} -lpthread
#  -lpanel -lncursesw -ltermkey)
install (TARGETS telegram-bot
    RUNTIME DESTINATION bin)
