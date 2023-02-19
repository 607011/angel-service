cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -ggdb -DDEBUG -pedantic -Wall -Wextra -Wconversion -Wreorder -Wuninitialized -Wtype-limits -Wno-pragmas")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -Wno-deprecated")

project(script-webservice)

set(BOOST_ROOT $ENV{BOOST_ROOT})
find_package(Boost 1.81.0 REQUIRED COMPONENTS regex url)
message(STATUS "Boost version: ${Boost_VERSION}")
message(STATUS "Boost include dirs: ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost lib dirs: ${Boost_LIBRARY_DIRS}")
message(STATUS "Boost libs: ${Boost_LIBRARIES}")

add_executable(script-webservice 
  main.cpp
  httpworker.cpp
  helper.cpp
  3rdparty/angelscript/add_on/scriptstdstring/scriptstdstring.cpp
  3rdparty/angelscript/add_on/scriptmath/scriptmath.cpp
)
set_target_properties(script-webservice PROPERTIES LINK_FLAGS_RELEASE "-dead_strip")

if(UNIX)
  set(PLATFORM_DEPENDENT_LIBRARIES, "-lpthread")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -pthread -O0 -ggdb")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -pthread -O2")
else()
  set(PLATFORM_DEPENDENT_LIBRARIES, "")
endif()

add_custom_command(TARGET script-webservice 
  POST_BUILD
  COMMAND strip script-webservice)

target_include_directories(script-webservice
	PRIVATE ${PROJECT_INCLUDE_DIRS}
  "3rdparty/angelscript/angelscript/include"
  "3rdparty/angelscript/add_on"
	PUBLIC ${Boost_INCLUDE_DIRS})

add_subdirectory(3rdparty/angelscript/angelscript/projects/cmake)
# add_library(angelscript STATIC ${CMAKE_SOURCE_DIR}/3rdparty/angelscript/angelscript/source)

target_link_libraries(script-webservice
	${Boost_LIBRARIES}
  ${CMAKE_SOURCE_DIR}/3rdparty/angelscript/angelscript/projects/cmake/libangelscript.a
)

add_custom_command(TARGET script-webservice
  POST_BUILD
  COMMAND strip script-webservice)

install(TARGETS script-webservice RUNTIME DESTINATION bin)
