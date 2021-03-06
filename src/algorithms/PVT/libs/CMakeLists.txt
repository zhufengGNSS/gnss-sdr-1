# Copyright (C) 2012-2019  (see AUTHORS file for a list of contributors)
#
# This file is part of GNSS-SDR.
#
# GNSS-SDR is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# GNSS-SDR is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with GNSS-SDR. If not, see <https://www.gnu.org/licenses/>.
#


set(PVT_LIB_SOURCES
    pvt_solution.cc
    ls_pvt.cc
    hybrid_ls_pvt.cc
    kml_printer.cc
    gpx_printer.cc
    rinex_printer.cc
    nmea_printer.cc
    rtcm_printer.cc
    rtcm.cc
    geojson_printer.cc
    rtklib_solver.cc
    pvt_conf.cc
    monitor_pvt_udp_sink.cc
)

set(PVT_LIB_HEADERS
    pvt_solution.h
    ls_pvt.h
    hybrid_ls_pvt.h
    kml_printer.h
    gpx_printer.h
    rinex_printer.h
    nmea_printer.h
    rtcm_printer.h
    rtcm.h
    geojson_printer.h
    rtklib_solver.h
    pvt_conf.h
    monitor_pvt_udp_sink.h
    monitor_pvt.h
)

list(SORT PVT_LIB_HEADERS)
list(SORT PVT_LIB_SOURCES)

source_group(Headers FILES ${PVT_LIB_HEADERS})

add_library(pvt_libs ${PVT_LIB_SOURCES} ${PVT_LIB_HEADERS})

target_link_libraries(pvt_libs
    PUBLIC
        Armadillo::armadillo
        Boost::date_time
        algorithms_libs_rtklib
        core_system_parameters
    PRIVATE
        algorithms_libs
        Boost::filesystem
        Boost::system
        Gflags::gflags
        Glog::glog
        Matio::matio
)

target_include_directories(pvt_libs
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src/core/receiver
)

target_compile_definitions(pvt_libs PRIVATE -DGNSS_SDR_VERSION="${VERSION}")

if(OS_IS_MACOSX)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")  # not AppleClang
        target_compile_definitions(pvt_libs
            PUBLIC
                -DBOOST_ASIO_HAS_STD_STRING_VIEW
        )
    endif()
endif()

if(ENABLE_CLANG_TIDY)
    if(CLANG_TIDY_EXE)
        set_target_properties(pvt_libs
            PROPERTIES
                CXX_CLANG_TIDY "${DO_CLANG_TIDY}"
        )
    endif()
endif()

set_property(TARGET pvt_libs APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/algorithms/libs>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/algorithms/libs/rtklib>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/core/system_parameters>
)
