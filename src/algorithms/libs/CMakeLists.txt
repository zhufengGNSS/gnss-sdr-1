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

add_subdirectory(rtklib)

set(GNSS_SPLIBS_SOURCES
    gps_l2c_signal.cc
    gps_l5_signal.cc
    galileo_e1_signal_processing.cc
    gnss_signal_processing.cc
    gps_sdr_signal_processing.cc
    glonass_l1_signal_processing.cc
    glonass_l2_signal_processing.cc
    pass_through.cc
    galileo_e5_signal_processing.cc
    beidou_b1i_signal_processing.cc
    beidou_b3i_signal_processing.cc
    complex_byte_to_float_x2.cc
    byte_x2_to_complex_byte.cc
    cshort_to_float_x2.cc
    short_x2_to_cshort.cc
    complex_float_to_complex_byte.cc
    conjugate_cc.cc
    conjugate_sc.cc
    conjugate_ic.cc
    gnss_sdr_create_directory.cc
    geofunctions.cc
)

set(GNSS_SPLIBS_HEADERS
    gps_l2c_signal.h
    gps_l5_signal.h
    galileo_e1_signal_processing.h
    gnss_signal_processing.h
    gps_sdr_signal_processing.h
    glonass_l1_signal_processing.h
    glonass_l2_signal_processing.h
    pass_through.h
    galileo_e5_signal_processing.h
    beidou_b1i_signal_processing.h
    beidou_b3i_signal_processing.h
    complex_byte_to_float_x2.h
    byte_x2_to_complex_byte.h
    cshort_to_float_x2.h
    short_x2_to_cshort.h
    complex_float_to_complex_byte.h
    conjugate_cc.h
    conjugate_sc.h
    conjugate_ic.h
    gnss_sdr_create_directory.h
    gnss_circular_deque.h
    geofunctions.h
)

if(OPENCL_FOUND)
    set(GNSS_SPLIBS_SOURCES ${GNSS_SPLIBS_SOURCES}
        opencl/fft_execute.cc # Needs OpenCL
        opencl/fft_setup.cc # Needs OpenCL
        opencl/fft_kernelstring.cc # Needs OpenCL
    )
    include_directories(${OPENCL_INCLUDE_DIRS})
    if(OS_IS_MACOSX)
        set(OPT_LIBRARIES ${OPT_LIBRARIES} "-framework OpenCL")
    else()
        set(OPT_LIBRARIES ${OPT_LIBRARIES} ${OPENCL_LIBRARIES})
    endif()
endif()

list(SORT GNSS_SPLIBS_HEADERS)
list(SORT GNSS_SPLIBS_SOURCES)

source_group(Headers FILES ${GNSS_SPLIBS_HEADERS})

add_library(algorithms_libs ${GNSS_SPLIBS_SOURCES} ${GNSS_SPLIBS_HEADERS})

target_link_libraries(algorithms_libs
    PUBLIC
        Armadillo::armadillo
        Boost::boost
        Gflags::gflags
        Gnuradio::runtime
        Gnuradio::blocks
        ${OPT_LIBRARIES}
    PRIVATE
        core_system_parameters
        Volk::volk ${ORC_LIBRARIES}
        Volkgnsssdr::volkgnsssdr
        Boost::filesystem
        Glog::glog
)

target_include_directories(algorithms_libs
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src/core/interfaces
)

target_compile_definitions(algorithms_libs
    PUBLIC -DGNSSSDR_INSTALL_DIR="${CMAKE_INSTALL_PREFIX}"
)

set_property(TARGET algorithms_libs
    APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

if(ENABLE_CLANG_TIDY)
    if(CLANG_TIDY_EXE)
        set_target_properties(algorithms_libs
            PROPERTIES
                CXX_CLANG_TIDY "${DO_CLANG_TIDY}"
        )
    endif()
endif()

###############################################################################

source_group(Headers FILES gnss_sdr_flags.h)

add_library(gnss_sdr_flags gnss_sdr_flags.cc gnss_sdr_flags.h)

target_link_libraries(gnss_sdr_flags
    PUBLIC
        Gflags::gflags
    PRIVATE
        Boost::filesystem
)

if(${GFLAGS_GREATER_20})
    target_compile_definitions(gnss_sdr_flags PRIVATE -DGFLAGS_GREATER_2_0=1)
endif()

target_compile_definitions(gnss_sdr_flags
    PRIVATE -DGNSSSDR_INSTALL_DIR="${CMAKE_INSTALL_PREFIX}"
)

if(ENABLE_CLANG_TIDY)
    if(CLANG_TIDY_EXE)
        set_target_properties(gnss_sdr_flags
            PROPERTIES
                CXX_CLANG_TIDY "${DO_CLANG_TIDY}"
        )
    endif()
endif()

set_property(TARGET gnss_sdr_flags
    APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
