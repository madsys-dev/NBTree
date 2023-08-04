cmake_minimum_required(VERSION 3.16.3)
project(NBTree)

#set(CMAKE_CXX_FLAGS "-std=c++11 ${CMAKE_CXX_FLAGS} -DPERF_LATENCY -DUSE_NVM_MALLOC -DNO_CONCURRENT -DNDEBUG -g -O2 -mrtm")
set(CMAKE_CXX_FLAGS "-std=c++11 ${CMAKE_CXX_FLAGS}  -DUSE_NVM_MALLOC -DCLEAR_NVM_POOL -g -O2 -mrtm")

link_directories("${PROJECT_SOURCE_DIR}/third-party-lib")
link_libraries("${PROJECT_SOURCE_DIR}/third-party-lib/libtbb.so")
link_directories()
link_libraries(boost_system boost_thread pthread pmemobj pmem)

include_directories("third-party-lib")
include_directories("include")

# add_executable(test
#	src/btree.cpp
#)

add_library(
	btree
	STATIC
	src/btree.cpp
)
install (TARGETS btree DESTINATION .)