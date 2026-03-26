# Copyright (c) Advanced Micro Devices, Inc.
# SPDX-License-Identifier:  MIT

add_library(
    pmc-gpu-collector-tests
    OBJECT
    test_device_construction.cpp
    test_device_metrics_power_through_memory.cpp
    test_device_metrics_vcn_jpeg_xcp.cpp
    test_device_metrics_xgmi_pcie_sdma.cpp
    test_device_support_detection.cpp
    test_device_errors_and_integration.cpp
)

target_include_directories(pmc-gpu-collector-tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}")

target_link_libraries(
    pmc-gpu-collector-tests
    PUBLIC
        rocprofiler-systems-pmc-library
        rocprofiler-systems-googletest-library
        rocprofiler-systems-logger
        rocprofiler-systems::rocprofiler-systems-headers
        rocprofiler-systems::rocprofiler-systems-rocm
        rocprofiler-systems::rocprofiler-systems-timemory
)
