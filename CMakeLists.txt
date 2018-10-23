cmake_minimum_required(VERSION 3.5)
project(sps)

set(CMAKE_CXX_STANDARD 11)

include_directories(../brpc/output/include .)

set(SOURCES
        sps.pb.cc
        sps.pb.h
        sps_bucket.cpp
        sps_bucket.h
        )

add_executable(sps_server
        ${SOURCES}
        sps_server.cpp
        )

add_executable(sps_test
        ${SOURCES}
        sps_test.cpp
        )