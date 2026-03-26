// Copyright (c) 2018-2025 Advanced Micro Devices, Inc. All Rights Reserved.
// SPDX-License-Identifier:  MIT

#include "device_test_fixture.hpp"

namespace rocprofsys::pmc::collectors::gpu::testing
{
// ============================================================================
// Category 9: Supported Metrics Detection Tests
// ============================================================================

/**
 * All Metrics Supported Detection
 *
 * Objective: Verify all supported bits are set when all metrics are valid.
 */
TEST_F(DeviceTest, all_metrics_supported_detection)
{
    // Setup: All metrics have valid values (non-sentinel)
    SetupAllMetricsSupported();

    // Create device (this triggers initialize_supported_metrics())
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // Verify all metric support bits are set
    auto supported = dev.get_supported_metrics();
    EXPECT_TRUE(supported.bits.current_socket_power);
    EXPECT_TRUE(supported.bits.average_socket_power);
    EXPECT_TRUE(supported.bits.memory_usage);
    EXPECT_TRUE(supported.bits.hotspot_temperature);
    EXPECT_TRUE(supported.bits.edge_temperature);
    EXPECT_TRUE(supported.bits.gfx_activity);
    EXPECT_TRUE(supported.bits.umc_activity);
    EXPECT_TRUE(supported.bits.mm_activity);
    // CreateValidMetrics sets per-XCP VCN/JPEG busy, so vcn_busy/jpeg_busy should be set
    // Device-level vcn_activity/jpeg_activity should NOT be set when per-XCP is available
    EXPECT_TRUE(supported.bits.vcn_busy);
    EXPECT_TRUE(supported.bits.jpeg_busy);
    EXPECT_FALSE(supported.bits.vcn_activity);
    EXPECT_FALSE(supported.bits.jpeg_activity);
    EXPECT_TRUE(supported.bits.xgmi);
    EXPECT_TRUE(supported.bits.pcie);
}

/**
 * VCN Activity Support Detection - Any XCP
 *
 * Objective: Verify VCN marked supported if any XCP has valid values.
 */
TEST_F(DeviceTest, vcn_activity_support_detection_any_xcp)
{
    // Setup: Only XCP 7 has valid VCN values, all others are sentinels
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();

    // Set valid VCN value only in XCP 7
    metrics.xcp_stats[7].vcn_busy[0] = 50;  // Valid value

    EXPECT_CALL(*mock_driver, get_metrics_info(test_handle, _))
        .Times(AtLeast(1))
        .WillRepeatedly(DoAll(SetArgPointee<1>(metrics), Return(AMDSMI_STATUS_SUCCESS)));

    uint64_t sentinel_mem = 0xFFFFFFFFFFFFFFFFULL;
    EXPECT_CALL(*mock_driver, get_memory_usage(test_handle, AMDSMI_MEM_TYPE_VRAM, _))
        .Times(AtLeast(1))
        .WillRepeatedly(
            DoAll(SetArgPointee<2>(sentinel_mem), Return(AMDSMI_STATUS_SUCCESS)));

    SetupSDMAExpectations(mock_driver, test_handle);

    // Create device
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // Verify VCN busy (per-XCP) is marked as supported
    EXPECT_TRUE(dev.get_supported_metrics().bits.vcn_busy);
    // Device-level vcn_activity should NOT be set when per-XCP is available
    EXPECT_FALSE(dev.get_supported_metrics().bits.vcn_activity);
}

/**
 * VCN Activity Unsupported - All Sentinels
 *
 * Objective: Verify VCN not supported when all XCPs have sentinel values.
 */
TEST_F(DeviceTest, vcn_activity_unsupported_all_sentinels)
{
    // Setup: All VCN values in all XCPs are sentinels
    SetupNoMetricsSupported();

    // Create device
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // Verify VCN activity is NOT supported
    EXPECT_FALSE(dev.get_supported_metrics().bits.vcn_activity);
}

/**
 * JPEG Activity Support Detection - Any XCP
 *
 * Objective: Verify JPEG marked supported if any XCP has valid values.
 */
TEST_F(DeviceTest, jpeg_activity_support_detection_any_xcp)
{
    // Setup: Only XCP 5 has valid JPEG values, all others are sentinels
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();

    // Set valid JPEG value only in XCP 5
    metrics.xcp_stats[5].jpeg_busy[0] = 75;  // Valid value

    EXPECT_CALL(*mock_driver, get_metrics_info(test_handle, _))
        .Times(AtLeast(1))
        .WillRepeatedly(DoAll(SetArgPointee<1>(metrics), Return(AMDSMI_STATUS_SUCCESS)));

    uint64_t sentinel_mem = 0xFFFFFFFFFFFFFFFFULL;
    EXPECT_CALL(*mock_driver, get_memory_usage(test_handle, AMDSMI_MEM_TYPE_VRAM, _))
        .Times(AtLeast(1))
        .WillRepeatedly(
            DoAll(SetArgPointee<2>(sentinel_mem), Return(AMDSMI_STATUS_SUCCESS)));

    SetupSDMAExpectations(mock_driver, test_handle);

    // Create device
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // Verify JPEG busy (per-XCP) is marked as supported
    EXPECT_TRUE(dev.get_supported_metrics().bits.jpeg_busy);
    // Device-level jpeg_activity should NOT be set when per-XCP is available
    EXPECT_FALSE(dev.get_supported_metrics().bits.jpeg_activity);
}

/**
 * XGMI Support Detection - Link Width Only
 *
 * Objective: Verify XGMI supported if only link width is valid.
 */
TEST_F(DeviceTest, xgmi_support_detection_link_width_only)
{
    // Setup: Only XGMI link width is valid, everything else is sentinel
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();
    metrics.xgmi_link_width      = 16;  // Valid link width
    // xgmi_link_speed and all xgmi_read_data_acc remain sentinel

    EXPECT_CALL(*mock_driver, get_metrics_info(test_handle, _))
        .Times(AtLeast(1))
        .WillRepeatedly(DoAll(SetArgPointee<1>(metrics), Return(AMDSMI_STATUS_SUCCESS)));

    uint64_t sentinel_mem = 0xFFFFFFFFFFFFFFFFULL;
    EXPECT_CALL(*mock_driver, get_memory_usage(test_handle, AMDSMI_MEM_TYPE_VRAM, _))
        .Times(AtLeast(1))
        .WillRepeatedly(
            DoAll(SetArgPointee<2>(sentinel_mem), Return(AMDSMI_STATUS_SUCCESS)));

    SetupSDMAExpectations(mock_driver, test_handle);

    // Create device
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // Verify XGMI is marked as supported (OR logic)
    EXPECT_TRUE(dev.get_supported_metrics().bits.xgmi);
}

/**
 * XGMI Support Detection - Any Read Data Valid
 *
 * Objective: Verify XGMI supported if any read data is valid.
 */
TEST_F(DeviceTest, xgmi_support_detection_any_read_data_valid)
{
    // Setup: Only one XGMI read data value is valid
    amdsmi_gpu_metrics_t metrics  = CreateSentinelMetrics();
    metrics.xgmi_read_data_acc[2] = 1000;  // Valid read data at index 2
    // link width, link speed, and all other read data remain sentinel

    EXPECT_CALL(*mock_driver, get_metrics_info(test_handle, _))
        .Times(AtLeast(1))
        .WillRepeatedly(DoAll(SetArgPointee<1>(metrics), Return(AMDSMI_STATUS_SUCCESS)));

    uint64_t sentinel_mem = 0xFFFFFFFFFFFFFFFFULL;
    EXPECT_CALL(*mock_driver, get_memory_usage(test_handle, AMDSMI_MEM_TYPE_VRAM, _))
        .Times(AtLeast(1))
        .WillRepeatedly(
            DoAll(SetArgPointee<2>(sentinel_mem), Return(AMDSMI_STATUS_SUCCESS)));

    SetupSDMAExpectations(mock_driver, test_handle);

    // Create device
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // Verify XGMI is marked as supported (std::any_of logic)
    EXPECT_TRUE(dev.get_supported_metrics().bits.xgmi);
}

/**
 * PCIe Support Detection - Bandwidth Only
 *
 * Objective: Verify PCIe supported if only bandwidth accumulator is valid.
 */
TEST_F(DeviceTest, pcie_support_detection_bandwidth_only)
{
    // Setup: Only PCIe bandwidth accumulator is valid
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();
    metrics.pcie_bandwidth_acc   = 1000000;  // Valid bandwidth accumulator
    // pcie_link_width, pcie_link_speed, pcie_bandwidth_inst remain sentinel

    EXPECT_CALL(*mock_driver, get_metrics_info(test_handle, _))
        .Times(AtLeast(1))
        .WillRepeatedly(DoAll(SetArgPointee<1>(metrics), Return(AMDSMI_STATUS_SUCCESS)));

    uint64_t sentinel_mem = 0xFFFFFFFFFFFFFFFFULL;
    EXPECT_CALL(*mock_driver, get_memory_usage(test_handle, AMDSMI_MEM_TYPE_VRAM, _))
        .Times(AtLeast(1))
        .WillRepeatedly(
            DoAll(SetArgPointee<2>(sentinel_mem), Return(AMDSMI_STATUS_SUCCESS)));

    SetupSDMAExpectations(mock_driver, test_handle);

    // Create device
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // Verify PCIe is marked as supported (OR logic)
    EXPECT_TRUE(dev.get_supported_metrics().bits.pcie);
}

/**
 * Memory Usage Support Detection
 *
 * Objective: Verify memory usage support based on API success with valid value.
 */
TEST_F(DeviceTest, memory_usage_support_detection)
{
    // Setup: Memory API returns success with valid value
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();

    EXPECT_CALL(*mock_driver, get_metrics_info(test_handle, _))
        .Times(AtLeast(1))
        .WillRepeatedly(DoAll(SetArgPointee<1>(metrics), Return(AMDSMI_STATUS_SUCCESS)));

    uint64_t valid_mem_usage = 4096000000;  // 4GB
    EXPECT_CALL(*mock_driver, get_memory_usage(test_handle, AMDSMI_MEM_TYPE_VRAM, _))
        .Times(AtLeast(1))
        .WillRepeatedly(
            DoAll(SetArgPointee<2>(valid_mem_usage), Return(AMDSMI_STATUS_SUCCESS)));

    SetupSDMAExpectations(mock_driver, test_handle);

    // Create device
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // Verify memory usage is marked as supported
    EXPECT_TRUE(dev.get_supported_metrics().bits.memory_usage);
}

/**
 * Memory Usage Unsupported - API Failure
 *
 * Objective: Verify memory not supported when API fails.
 */
TEST_F(DeviceTest, memory_usage_unsupported_api_failure)
{
    // Setup: Memory API returns failure
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();

    EXPECT_CALL(*mock_driver, get_metrics_info(test_handle, _))
        .Times(AtLeast(1))
        .WillRepeatedly(DoAll(SetArgPointee<1>(metrics), Return(AMDSMI_STATUS_SUCCESS)));

    EXPECT_CALL(*mock_driver, get_memory_usage(test_handle, AMDSMI_MEM_TYPE_VRAM, _))
        .Times(AtLeast(1))
        .WillRepeatedly(Return(AMDSMI_STATUS_NOT_SUPPORTED));

    SetupSDMAExpectations(mock_driver, test_handle);

    // Create device
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // Verify memory usage is NOT marked as supported
    EXPECT_FALSE(dev.get_supported_metrics().bits.memory_usage);
}

/**
 * Memory Usage Unsupported - Sentinel Value
 *
 * Objective: Verify memory not supported when value is sentinel.
 */
TEST_F(DeviceTest, memory_usage_unsupported_sentinel_value)
{
    // Setup: Memory API returns success but with sentinel value
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();

    EXPECT_CALL(*mock_driver, get_metrics_info(test_handle, _))
        .Times(AtLeast(1))
        .WillRepeatedly(DoAll(SetArgPointee<1>(metrics), Return(AMDSMI_STATUS_SUCCESS)));

    uint64_t sentinel_mem = 0xFFFFFFFFFFFFFFFFULL;  // Sentinel value
    EXPECT_CALL(*mock_driver, get_memory_usage(test_handle, AMDSMI_MEM_TYPE_VRAM, _))
        .Times(AtLeast(1))
        .WillRepeatedly(
            DoAll(SetArgPointee<2>(sentinel_mem), Return(AMDSMI_STATUS_SUCCESS)));

    SetupSDMAExpectations(mock_driver, test_handle);

    // Create device
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // Verify memory usage is NOT marked as supported
    EXPECT_FALSE(dev.get_supported_metrics().bits.memory_usage);
}

}  // namespace rocprofsys::pmc::collectors::gpu::testing
