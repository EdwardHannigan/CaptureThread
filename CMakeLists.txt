cmake_minimum_required(VERSION 2.8.11)
project(CaptureThread)

set(srcs ${srcs} CaptureThread.cpp)
set(srcs ${srcs} lockfreequeue.h)

add_executable(CaptureThread ${srcs})

if(WIN32)
	add_definitions(-D_WINDOWS)
	add_definitions(-DWIN32)
	add_definitions(-DWINDOWS)

	target_link_libraries(CaptureThread winmm.lib)
else()
	add_definitions(-DLINUX)
	#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -std=c++0x")
endif()
