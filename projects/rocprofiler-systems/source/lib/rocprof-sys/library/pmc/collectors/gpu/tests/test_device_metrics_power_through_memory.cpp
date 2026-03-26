// Copyright (c) 2018-2025 Advanced Micro Devices, Inc. All Rights Reserved.
// SPDX-License-Identifier:  MIT

#include "device_test_fixture.hpp"

namespace rocprofsys::pmc::collectors::gpu::testing
{
// ============================================================================
// Category 2: Power Metrics Collection Tests
// ============================================================================

/**
 * TC2.1: Current Socket Power Collection
 *
 * Objective: Verify current power is collected when supported.
 */
TEST_F(DeviceTest, current_socket_power_collection)
{
    // Setup: Mock returns specific current power value
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();
    metrics.current_socket_power = 150;  // 150 watts

    EXPECT_CALL(*mock_driver, get_metrics_info(test_handle, _))
        .Times(AtLeast(1))
        .WillRepeatedly(DoAll(SetArgPointee<1>(metrics), Return(AMDSMI_STATUS_SUCCESS)));

    uint64_t sentinel_mem = 0xFFFFFFFFFFFFFFFFULL;
    EXPECT_CALL(*mock_driver, get_memory_usage(test_handle, AMDSMI_MEM_TYPE_VRAM, _))
        .Times(AtLeast(1))
        .WillRepeatedly(
            DoAll(SetArgPointee<2>(sentinel_mem), Return(AMDSMI_STATUS_SUCCESS)));

    SetupSDMAExpectations(mock_driver, test_handle);

    // Create device (initializes supported metrics)
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // Verify current_socket_power is marked as supported
    EXPECT_TRUE(dev.get_supported_metrics().bits.current_socket_power);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify current power value was collected
    EXPECT_EQ(collected_metrics.current_socket_power, 150U);
}

/**
 * TC2.2: Average Socket Power Collection
 *
 * Objective: Verify average power is collected when supported.
 */
TEST_F(DeviceTest, average_socket_power_collection)
{
    // Setup: Mock returns specific average power value
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();
    metrics.average_socket_power = 140;  // 140 watts

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

    // Verify average_socket_power is marked as supported
    EXPECT_TRUE(dev.get_supported_metrics().bits.average_socket_power);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify average power value was collected
    EXPECT_EQ(collected_metrics.average_socket_power, 140U);
}

/**
 * TC2.3: Power Metrics Not Collected When Unsupported
 *
 * Objective: Verify power metrics remain zero when not supported.
 */
TEST_F(DeviceTest, power_metrics_not_collected_when_unsupported)
{
    // Setup: All metrics are sentinel values (unsupported)
    SetupNoMetricsSupported();

    // Create device
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // Verify power metrics are not marked as supported
    auto supported = dev.get_supported_metrics();
    EXPECT_FALSE(supported.bits.current_socket_power);
    EXPECT_FALSE(supported.bits.average_socket_power);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify power values remain zero
    EXPECT_EQ(collected_metrics.current_socket_power, 0U);
    EXPECT_EQ(collected_metrics.average_socket_power, 0U);
}

// ============================================================================
// Category 3: Temperature Metrics Collection Tests
// ============================================================================

/**
 * TC2.4: Hotspot Temperature Collection
 *
 * Objective: Verify hotspot temperature collection.
 */
TEST_F(DeviceTest, hotspot_temperature_collection)
{
    // Setup: Mock returns specific hotspot temperature value
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();
    metrics.temperature_hotspot  = 75;  // 75°C

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

    // Verify hotspot_temperature is marked as supported
    EXPECT_TRUE(dev.get_supported_metrics().bits.hotspot_temperature);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify hotspot temperature value was collected
    EXPECT_EQ(collected_metrics.hotspot_temperature, 75);
}

/**
 * TC2.5: Edge Temperature Collection
 *
 * Objective: Verify edge temperature collection.
 */
TEST_F(DeviceTest, edge_temperature_collection)
{
    // Setup: Mock returns specific edge temperature value
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();
    metrics.temperature_edge     = 70;  // 70°C in degrees Celsius

    EXPECT_CALL(*mock_driver, get_metrics_info(test_handle, _))
        .Times(AtLeast(1))
        .WillRepeatedly(DoAll(SetArgPointee<1>(metrics), Return(AMDSMI_STATUS_SUCCESS)));

    constexpr uint64_t sentinel_mem = 0xFFFFFFFFFFFFFFFFULL;
    EXPECT_CALL(*mock_driver, get_memory_usage(test_handle, AMDSMI_MEM_TYPE_VRAM, _))
        .Times(AtLeast(1))
        .WillRepeatedly(
            DoAll(SetArgPointee<2>(sentinel_mem), Return(AMDSMI_STATUS_SUCCESS)));

    SetupSDMAExpectations(mock_driver, test_handle);

    // Create device
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // Verify edge_temperature is marked as supported
    EXPECT_TRUE(dev.get_supported_metrics().bits.edge_temperature);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify edge temperature value was collected (raw value from AMD SMI)
    EXPECT_EQ(collected_metrics.edge_temperature, 70);
}

/**
 * TC2.6: Temperature Metrics Not Collected When Unsupported
 *
 * Objective: Verify temperature skipped when not supported.
 */
TEST_F(DeviceTest, temperature_metrics_not_collected_when_unsupported)
{
    // Setup: All metrics are sentinel values (unsupported)
    SetupNoMetricsSupported();

    // Create device
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // Verify temperature metrics are not marked as supported
    auto supported = dev.get_supported_metrics();
    EXPECT_FALSE(supported.bits.hotspot_temperature);
    EXPECT_FALSE(supported.bits.edge_temperature);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify temperature values remain zero
    EXPECT_EQ(collected_metrics.hotspot_temperature, 0);
    EXPECT_EQ(collected_metrics.edge_temperature, 0);
}

// ============================================================================
// Category 4: Activity Metrics Collection Tests
// ============================================================================

/**
 * GFX Activity Collection
 *
 * Objective: Verify graphics engine activity collection.
 */
TEST_F(DeviceTest, gfx_activity_collection)
{
    // Setup: Mock returns specific GFX activity value
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();
    metrics.average_gfx_activity = 85;  // 85% activity

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

    // Verify gfx_activity is marked as supported
    EXPECT_TRUE(dev.get_supported_metrics().bits.gfx_activity);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify GFX activity value was collected
    EXPECT_EQ(collected_metrics.gfx_activity, 85U);
}

/**
 * UMC Activity Collection
 *
 * Objective: Verify memory controller activity collection.
 */
TEST_F(DeviceTest, umc_activity_collection)
{
    // Setup: Mock returns specific UMC activity value
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();
    metrics.average_umc_activity = 60;  // 60% activity

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

    // Verify umc_activity is marked as supported
    EXPECT_TRUE(dev.get_supported_metrics().bits.umc_activity);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify UMC activity value was collected
    EXPECT_EQ(collected_metrics.umc_activity, 60U);
}

/**
 * MM Activity Collection
 *
 * Objective: Verify multimedia activity collection.
 */
TEST_F(DeviceTest, mm_activity_collection)
{
    // Setup: Mock returns specific MM activity value
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();
    metrics.average_mm_activity  = 40;  // 40% activity

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

    // Verify mm_activity is marked as supported
    EXPECT_TRUE(dev.get_supported_metrics().bits.mm_activity);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify MM activity value was collected
    EXPECT_EQ(collected_metrics.mm_activity, 40U);
}

/**
 * All Activity Metrics Collection
 *
 * Objective: Verify all three activity metrics collected together.
 */
TEST_F(DeviceTest, all_activity_metrics_collection)
{
    // Setup: Mock returns all three activity values
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();
    metrics.average_gfx_activity = 85;
    metrics.average_umc_activity = 60;
    metrics.average_mm_activity  = 40;

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

    // Verify all activity metrics are marked as supported
    auto supported = dev.get_supported_metrics();
    EXPECT_TRUE(supported.bits.gfx_activity);
    EXPECT_TRUE(supported.bits.umc_activity);
    EXPECT_TRUE(supported.bits.mm_activity);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify all three activity values were collected correctly
    EXPECT_EQ(collected_metrics.gfx_activity, 85U);
    EXPECT_EQ(collected_metrics.umc_activity, 60U);
    EXPECT_EQ(collected_metrics.mm_activity, 40U);
}

// ============================================================================
// Category 5: Memory Usage Collection Tests
// ============================================================================

/**
 * VRAM Memory Usage Collection Success
 *
 * Objective: Verify VRAM usage collected when API succeeds.
 */
TEST_F(DeviceTest, vram_memory_usage_collection_success)
{
    // Setup: Mock returns sentinel for all GPU metrics
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();

    EXPECT_CALL(*mock_driver, get_metrics_info(test_handle, _))
        .Times(AtLeast(1))
        .WillRepeatedly(DoAll(SetArgPointee<1>(metrics), Return(AMDSMI_STATUS_SUCCESS)));

    // Mock returns valid memory usage (8 GB)
    uint64_t mem_usage = 8589934592ULL;
    EXPECT_CALL(*mock_driver, get_memory_usage(test_handle, AMDSMI_MEM_TYPE_VRAM, _))
        .Times(AtLeast(1))
        .WillRepeatedly(
            DoAll(SetArgPointee<2>(mem_usage), Return(AMDSMI_STATUS_SUCCESS)));

    SetupSDMAExpectations(mock_driver, test_handle);

    // Create device
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // Verify memory_usage is marked as supported
    EXPECT_TRUE(dev.get_supported_metrics().bits.memory_usage);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify memory usage value was collected
    EXPECT_EQ(collected_metrics.memory_usage, 8589934592ULL);
}

/**
 * Memory Usage Collection Failure
 *
 * Objective: Verify memory usage remains zero on API failure.
 */
TEST_F(DeviceTest, memory_usage_collection_failure)
{
    // Setup: Mock returns sentinel for all GPU metrics
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();

    EXPECT_CALL(*mock_driver, get_metrics_info(test_handle, _))
        .Times(AtLeast(1))
        .WillRepeatedly(DoAll(SetArgPointee<1>(metrics), Return(AMDSMI_STATUS_SUCCESS)));

    // Mock returns failure for memory usage
    EXPECT_CALL(*mock_driver, get_memory_usage(test_handle, AMDSMI_MEM_TYPE_VRAM, _))
        .Times(AtLeast(1))
        .WillRepeatedly(Return(AMDSMI_STATUS_NOT_SUPPORTED));

    SetupSDMAExpectations(mock_driver, test_handle);

    // Create device
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // Verify memory_usage is NOT marked as supported
    EXPECT_FALSE(dev.get_supported_metrics().bits.memory_usage);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify memory usage remains zero
    EXPECT_EQ(collected_metrics.memory_usage, 0ULL);
}

/**
 * Memory Usage Not Collected When Unsupported
 *
 * Objective: Verify early return when memory not supported.
 */
TEST_F(DeviceTest, memory_usage_not_collected_when_unsupported)
{
    // Setup: All metrics are sentinel values (unsupported)
    SetupNoMetricsSupported();

    // Create device
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // Verify memory_usage is NOT marked as supported
    EXPECT_FALSE(dev.get_supported_metrics().bits.memory_usage);

    // Mock should NOT be called for memory usage during collection
    // (because supported bit is false, early return happens)
    EXPECT_CALL(*mock_driver, get_memory_usage(test_handle, AMDSMI_MEM_TYPE_VRAM, _))
        .Times(0);  // Should not be called during get_gpu_metrics()

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify memory usage remains zero
    EXPECT_EQ(collected_metrics.memory_usage, 0ULL);
}

}  // namespace rocprofsys::pmc::collectors::gpu::testing
