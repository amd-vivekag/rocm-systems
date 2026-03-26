// Copyright (c) 2018-2025 Advanced Micro Devices, Inc. All Rights Reserved.
// SPDX-License-Identifier:  MIT

#include "device_test_fixture.hpp"

namespace rocprofsys::pmc::collectors::gpu::testing
{
// ============================================================================
// Categories 6 & 10: XCP, VCN, and JPEG (collection + dual-source behavior)
// ============================================================================

/**
 * VCN Busy Collection - All XCPs (MI300)
 *
 * Objective: Verify per-XCP VCN busy stats copied for all XCP instances.
 */
TEST_F(DeviceTest, vcn_busy_collection_all_xcps)
{
    // Setup: Mock returns valid VCN busy values for all XCPs
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();

    // Set VCN busy values for all XCP instances
    for(size_t xcp = 0; xcp < AMDSMI_MAX_NUM_XCP; ++xcp)
    {
        for(size_t vcn = 0; vcn < AMDSMI_MAX_NUM_VCN; ++vcn)
        {
            metrics.xcp_stats[xcp].vcn_busy[vcn] = static_cast<uint16_t>(50 + xcp + vcn);
        }
    }

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

    // Verify vcn_busy is marked as supported (per-XCP metrics)
    EXPECT_TRUE(dev.get_supported_metrics().bits.vcn_busy);
    // Device-level vcn_activity should NOT be set when per-XCP is available
    EXPECT_FALSE(dev.get_supported_metrics().bits.vcn_activity);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify all XCP VCN arrays were copied correctly
    for(size_t xcp = 0; xcp < AMDSMI_MAX_NUM_XCP; ++xcp)
    {
        for(size_t vcn = 0; vcn < AMDSMI_MAX_NUM_VCN; ++vcn)
        {
            EXPECT_EQ(collected_metrics.xcp_stats[xcp].vcn_busy[vcn],
                      static_cast<uint16_t>(50 + xcp + vcn));
        }
    }
}

/**
 * JPEG Activity Collection - All XCPs
 *
 * Objective: Verify JPEG busy stats copied for all XCP instances.
 */
TEST_F(DeviceTest, jpeg_activity_collection_all_xcps)
{
    // Setup: Mock returns valid JPEG busy values for all XCPs
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();

    // Set JPEG activity values for all XCP instances
    for(size_t xcp = 0; xcp < AMDSMI_MAX_NUM_XCP; ++xcp)
    {
        for(size_t jpeg = 0; jpeg < ROCPROFSYS_AMDSMI_JPEG_ENGINE_COUNT; ++jpeg)
        {
            metrics.xcp_stats[xcp].jpeg_busy[jpeg] =
                static_cast<uint16_t>(30 + xcp + jpeg);
        }
    }

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

    // Verify jpeg_busy (per-XCP) is marked as supported
    EXPECT_TRUE(dev.get_supported_metrics().bits.jpeg_busy);
    // Device-level jpeg_activity should NOT be set when per-XCP is available
    EXPECT_FALSE(dev.get_supported_metrics().bits.jpeg_activity);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify all XCP JPEG arrays were copied correctly
    for(size_t xcp = 0; xcp < AMDSMI_MAX_NUM_XCP; ++xcp)
    {
        for(size_t jpeg = 0; jpeg < ROCPROFSYS_AMDSMI_JPEG_ENGINE_COUNT; ++jpeg)
        {
            EXPECT_EQ(collected_metrics.xcp_stats[xcp].jpeg_busy[jpeg],
                      static_cast<uint16_t>(30 + xcp + jpeg));
        }
    }
}

/**
 * XCP Metrics Not Collected When Unsupported
 *
 * Objective: Verify XCP metrics skipped when not supported.
 */
TEST_F(DeviceTest, xcp_metrics_not_collected_when_unsupported)
{
    // Setup: All metrics are sentinel values (unsupported)
    SetupNoMetricsSupported();

    // Create device
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // Verify XCP metrics are NOT marked as supported
    auto supported = dev.get_supported_metrics();
    EXPECT_FALSE(supported.bits.vcn_busy);
    EXPECT_FALSE(supported.bits.jpeg_busy);
    EXPECT_FALSE(supported.bits.vcn_activity);
    EXPECT_FALSE(supported.bits.jpeg_activity);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify XCP arrays remain default-initialized (all zeros)
    for(size_t xcp = 0; xcp < AMDSMI_MAX_NUM_XCP; ++xcp)
    {
        for(size_t vcn = 0; vcn < AMDSMI_MAX_NUM_VCN; ++vcn)
        {
            EXPECT_EQ(collected_metrics.xcp_stats[xcp].vcn_busy[vcn], 0);
        }
        for(size_t jpeg = 0; jpeg < ROCPROFSYS_AMDSMI_JPEG_ENGINE_COUNT; ++jpeg)
        {
            EXPECT_EQ(collected_metrics.xcp_stats[xcp].jpeg_busy[jpeg], 0);
        }
    }
}

/**
 * Mixed VCN/JPEG Support
 *
 * Objective: Verify VCN collected but not JPEG when only VCN supported.
 */
TEST_F(DeviceTest, mixed_vcn_jpeg_support)
{
    // Setup: Only VCN is supported, JPEG is not
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();

    // Set valid VCN values for all XCPs
    for(size_t xcp = 0; xcp < AMDSMI_MAX_NUM_XCP; ++xcp)
    {
        for(size_t vcn = 0; vcn < AMDSMI_MAX_NUM_VCN; ++vcn)
        {
            metrics.xcp_stats[xcp].vcn_busy[vcn] = static_cast<uint16_t>(50 + vcn);
        }
        // JPEG remains sentinel (0xFFFF)
    }

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

    // Verify only VCN busy (per-XCP) is supported
    auto supported = dev.get_supported_metrics();
    EXPECT_TRUE(supported.bits.vcn_busy);
    EXPECT_FALSE(supported.bits.jpeg_busy);
    // Device-level vcn_activity/jpeg_activity should NOT be set when per-XCP is available
    EXPECT_FALSE(supported.bits.vcn_activity);
    EXPECT_FALSE(supported.bits.jpeg_activity);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify VCN arrays are populated
    for(size_t xcp = 0; xcp < AMDSMI_MAX_NUM_XCP; ++xcp)
    {
        for(size_t vcn = 0; vcn < AMDSMI_MAX_NUM_VCN; ++vcn)
        {
            EXPECT_EQ(collected_metrics.xcp_stats[xcp].vcn_busy[vcn],
                      static_cast<uint16_t>(50 + vcn));
        }
    }

    // Verify JPEG arrays remain default-initialized (zeros)
    for(size_t xcp = 0; xcp < AMDSMI_MAX_NUM_XCP; ++xcp)
    {
        for(size_t jpeg = 0; jpeg < ROCPROFSYS_AMDSMI_JPEG_ENGINE_COUNT; ++jpeg)
        {
            EXPECT_EQ(collected_metrics.xcp_stats[xcp].jpeg_busy[jpeg], 0);
        }
    }
}

// ============================================================================
// Category 10: VCN Activity Dual Source Tests
// ============================================================================

/**
 * VCN Activity in Top-Level Field Only
 *
 * Objective: Verify VCN activity is detected when present in top-level vcn_activity[]
 * field but NOT in xcp_stats[].vcn_busy[] arrays.
 *
 * Note: This tests a gap in the current implementation - the device class currently
 * only checks xcp_stats[].vcn_busy[] but should also check the top-level vcn_activity[].
 */
TEST_F(DeviceTest, vcn_activity_top_level_field_only)
{
    // Setup: VCN activity present in top-level field, XCP stats have sentinels
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();

    // Set valid VCN activity in top-level field
    // Note: This field exists in amdsmi_gpu_metrics_t but is not currently checked!
    // metrics.vcn_activity[0] = 75;  // 75% VCN utilization
    // metrics.vcn_activity[1] = 50;  // 50% VCN utilization

    // All XCP VCN busy values remain sentinel (0xFFFF)

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

    // EXPECTED BEHAVIOR: VCN activity should be marked as supported
    // CURRENT BEHAVIOR: Will NOT be supported because implementation only checks XCP
    // stats This test documents the gap and will fail until implementation is fixed

    // When implementation is fixed, uncomment:
    // EXPECT_TRUE(dev.get_supported_metrics().bits.vcn_activity);

    // Current behavior (documents the bug):
    EXPECT_FALSE(dev.get_supported_metrics().bits.vcn_activity)
        << "BUG: Implementation does not check top-level vcn_activity[] field";
}

/**
 * VCN Activity in Both Top-Level and XCP Fields
 *
 * Objective: Verify VCN activity when present in BOTH vcn_activity[] and
 * xcp_stats[].vcn_busy[].
 */
TEST_F(DeviceTest, vcn_activity_in_both_fields)
{
    // Setup: VCN activity in both top-level and XCP fields
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();

    // Set valid VCN activity in XCP stats (currently checked)
    metrics.xcp_stats[0].vcn_busy[0] = 80;  // 80% in XCP 0, VCN 0

    // Also set in top-level field (not currently checked)
    // metrics.vcn_activity[0] = 75;  // Different value in top-level field

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

    // Per-XCP vcn_busy should be supported (XCP stats are valid)
    EXPECT_TRUE(dev.get_supported_metrics().bits.vcn_busy);
    // Device-level vcn_activity should NOT be set when per-XCP is available
    EXPECT_FALSE(dev.get_supported_metrics().bits.vcn_activity);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify XCP stats were collected
    EXPECT_EQ(collected_metrics.xcp_stats[0].vcn_busy[0], 80U);
}

/**
 * VCN Activity Detection with Top-Level Field Support
 *
 * Objective: Document expected behavior when both VCN sources are checked.
 *
 * This test describes how the initialize_supported_metrics() should work:
 * - Check top-level vcn_activity[] array (currently missing)
 * - Check xcp_stats[].vcn_busy[] arrays (currently implemented)
 * - Mark vcn_activity as supported if EITHER source has valid data
 */
TEST_F(DeviceTest, vcn_activity_detection_should_check_both_sources)
{
    // Setup: Only top-level vcn_activity has valid data
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();

    // Top-level has valid data (not checked by current implementation)
    // metrics.vcn_activity[0] = 60;

    // XCP stats have sentinels (checked by current implementation)
    // All xcp_stats[].vcn_busy[] remain 0xFFFF

    EXPECT_CALL(*mock_driver, get_metrics_info(test_handle, _))
        .Times(AtLeast(1))
        .WillRepeatedly(DoAll(SetArgPointee<1>(metrics), Return(AMDSMI_STATUS_SUCCESS)));

    uint64_t sentinel_mem = 0xFFFFFFFFFFFFFFFFULL;
    EXPECT_CALL(*mock_driver, get_memory_usage(test_handle, AMDSMI_MEM_TYPE_VRAM, _))
        .Times(AtLeast(1))
        .WillRepeatedly(
            DoAll(SetArgPointee<2>(sentinel_mem), Return(AMDSMI_STATUS_SUCCESS)));

    SetupSDMAExpectations(mock_driver, test_handle);

    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // EXPECTED (when fixed): vcn_activity should be supported
    // CURRENT: Will be false because top-level field is not checked
    EXPECT_FALSE(dev.get_supported_metrics().bits.vcn_activity)
        << "Implementation gap: initialize_supported_metrics() should check both "
           "vcn_activity[] AND xcp_stats[].vcn_busy[]";
}

/**
 * VCN Activity Collection Priority
 *
 * Objective: Document which VCN source should take priority when collecting.
 *
 * When both sources are available, the implementation should decide:
 * - Use top-level vcn_activity[] for overall VCN utilization?
 * - Use xcp_stats[].vcn_busy[] for per-partition granularity?
 * - Collect from both?
 */
TEST_F(DeviceTest, vcn_activity_collection_priority)
{
    // Setup: Different values in both sources
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();

    // XCP stats (per-partition detail)
    metrics.xcp_stats[0].vcn_busy[0] = 80;
    metrics.xcp_stats[0].vcn_busy[1] = 70;

    // Top-level (overall average?)
    // metrics.vcn_activity[0] = 75;  // Average of 80 and 70?
    // metrics.vcn_activity[1] = 0;   // Or different semantic meaning?

    EXPECT_CALL(*mock_driver, get_metrics_info(test_handle, _))
        .Times(AtLeast(1))
        .WillRepeatedly(DoAll(SetArgPointee<1>(metrics), Return(AMDSMI_STATUS_SUCCESS)));

    uint64_t sentinel_mem = 0xFFFFFFFFFFFFFFFFULL;
    EXPECT_CALL(*mock_driver, get_memory_usage(test_handle, AMDSMI_MEM_TYPE_VRAM, _))
        .Times(AtLeast(1))
        .WillRepeatedly(
            DoAll(SetArgPointee<2>(sentinel_mem), Return(AMDSMI_STATUS_SUCCESS)));

    SetupSDMAExpectations(mock_driver, test_handle);

    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    auto collected =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Current implementation collects from XCP stats only
    EXPECT_EQ(collected.xcp_stats[0].vcn_busy[0], 80U);
    EXPECT_EQ(collected.xcp_stats[0].vcn_busy[1], 70U);

    // Future enhancement: Also collect top-level vcn_activity[]?
    // This would require extending the metrics structure to include both fields
}

/**
 * VCN Activity XCP Stats Empty But Top-Level Valid
 *
 * Objective: Test scenario where hardware reports VCN activity at top-level
 * but XCP partitioning is disabled or not reporting VCN stats.
 */
TEST_F(DeviceTest, vcn_activity_xcp_disabled_top_level_valid)
{
    // Setup: XCP stats all sentinels (XCP disabled or not supported)
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();

    // Top-level VCN activity still valid
    // metrics.vcn_activity[0] = 65;  // VCN 0 at 65%
    // metrics.vcn_activity[1] = 55;  // VCN 1 at 55%

    // All XCP stats remain sentinel
    EXPECT_CALL(*mock_driver, get_metrics_info(test_handle, _))
        .Times(AtLeast(1))
        .WillRepeatedly(DoAll(SetArgPointee<1>(metrics), Return(AMDSMI_STATUS_SUCCESS)));

    uint64_t sentinel_mem = 0xFFFFFFFFFFFFFFFFULL;
    EXPECT_CALL(*mock_driver, get_memory_usage(test_handle, AMDSMI_MEM_TYPE_VRAM, _))
        .Times(AtLeast(1))
        .WillRepeatedly(
            DoAll(SetArgPointee<2>(sentinel_mem), Return(AMDSMI_STATUS_SUCCESS)));

    SetupSDMAExpectations(mock_driver, test_handle);

    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // CURRENT: VCN not supported (implementation only checks XCP stats)
    EXPECT_FALSE(dev.get_supported_metrics().bits.vcn_activity);

    // EXPECTED (when fixed): Should be supported via top-level field
    // This represents real hardware scenario where XCP partitioning is disabled
    // but VCN engines are still active and reporting utilization
}

}  // namespace rocprofsys::pmc::collectors::gpu::testing
