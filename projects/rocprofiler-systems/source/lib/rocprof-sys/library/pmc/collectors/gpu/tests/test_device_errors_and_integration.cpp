// Copyright (c) 2018-2025 Advanced Micro Devices, Inc. All Rights Reserved.
// SPDX-License-Identifier:  MIT

#include "device_test_fixture.hpp"

namespace rocprofsys::pmc::collectors::gpu::testing
{
// ============================================================================
// Category 11: Error Handling and Edge Cases
// ============================================================================

/**
 * get_metrics_info() Failure
 *
 * Objective: Verify graceful handling when metrics info unavailable.
 */
TEST_F(DeviceTest, get_metrics_info_failure)
{
    // Setup: get_metrics_info() returns failure
    EXPECT_CALL(*mock_driver, get_metrics_info(test_handle, _))
        .Times(AtLeast(1))
        .WillRepeatedly(Return(AMDSMI_STATUS_NOT_SUPPORTED));

    uint64_t valid_mem_usage = 4096000000;
    EXPECT_CALL(*mock_driver, get_memory_usage(test_handle, AMDSMI_MEM_TYPE_VRAM, _))
        .Times(AtLeast(1))
        .WillRepeatedly(
            DoAll(SetArgPointee<2>(valid_mem_usage), Return(AMDSMI_STATUS_SUCCESS)));

    // Create device
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // Call get_gpu_metrics() - should not throw
    auto metrics = dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify returns default-initialized metrics (all zeros)
    EXPECT_EQ(metrics.current_socket_power, 0U);
    EXPECT_EQ(metrics.average_socket_power, 0U);
    EXPECT_EQ(metrics.hotspot_temperature, 0);
    EXPECT_EQ(metrics.edge_temperature, 0);
    EXPECT_EQ(metrics.gfx_activity, 0U);
}

/**
 * get_metrics_info() Failure During Initialization
 *
 * Objective: Verify initialization handles metrics info failure.
 */
TEST_F(DeviceTest, get_metrics_info_failure_during_init)
{
    // Setup: get_metrics_info() returns failure during construction
    EXPECT_CALL(*mock_driver, get_metrics_info(test_handle, _))
        .Times(AtLeast(1))
        .WillRepeatedly(Return(AMDSMI_STATUS_NOT_SUPPORTED));

    uint64_t valid_mem_usage = 4096000000;
    EXPECT_CALL(*mock_driver, get_memory_usage(test_handle, AMDSMI_MEM_TYPE_VRAM, _))
        .Times(AtLeast(1))
        .WillRepeatedly(
            DoAll(SetArgPointee<2>(valid_mem_usage), Return(AMDSMI_STATUS_SUCCESS)));

    // Create device - should not crash
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // is_supported() should reflect whether ANY metric was supported (memory in this
    // case)
    EXPECT_TRUE(dev.is_supported());

    // Verify memory is supported but GPU metrics are not
    auto supported = dev.get_supported_metrics();
    EXPECT_TRUE(supported.bits.memory_usage);
    EXPECT_FALSE(supported.bits.current_socket_power);
}

/**
 * Multiple Metric Collections
 *
 * Objective: Verify device can collect metrics multiple times.
 */
TEST_F(DeviceTest, multiple_metric_collections)
{
    // Setup: Mock returns varying values across collections
    SetupAllMetricsSupported();

    // Create device
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // Collect metrics 10 times in a row
    for(int i = 0; i < 10; ++i)
    {
        auto metrics =
            dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);
        // Each collection should succeed
        EXPECT_GT(metrics.current_socket_power, 0U);
    }
}

/**
 * Large Array Indices - XGMI
 *
 * Objective: Verify no buffer overflow with maximum XGMI links.
 */
TEST_F(DeviceTest, large_array_indices_xgmi)
{
    // Setup: Set all AMDSMI_MAX_NUM_XGMI_LINKS entries
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();

    for(size_t i = 0; i < AMDSMI_MAX_NUM_XGMI_LINKS; ++i)
    {
        metrics.xgmi_read_data_acc[i]  = 1000 + i;
        metrics.xgmi_write_data_acc[i] = 2000 + i;
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

    // Collect metrics - should not crash or cause buffer overflow
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify all links were processed correctly
    for(size_t i = 0; i < AMDSMI_MAX_NUM_XGMI_LINKS; ++i)
    {
        EXPECT_EQ(collected_metrics.xgmi.data_acc.read[i], 1000 + i);
        EXPECT_EQ(collected_metrics.xgmi.data_acc.write[i], 2000 + i);
    }
}

/**
 * Large Array Indices - XCP
 *
 * Objective: Verify no buffer overflow with maximum XCPs.
 */
TEST_F(DeviceTest, large_array_indices_xcp)
{
    // Setup: Set all AMDSMI_MAX_NUM_XCP entries
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();

    for(size_t xcp = 0; xcp < AMDSMI_MAX_NUM_XCP; ++xcp)
    {
        for(size_t vcn = 0; vcn < AMDSMI_MAX_NUM_VCN; ++vcn)
        {
            metrics.xcp_stats[xcp].vcn_busy[vcn] = static_cast<uint16_t>(xcp * 10 + vcn);
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

    // Collect metrics - should not crash
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify all XCP stats were processed correctly
    for(size_t xcp = 0; xcp < AMDSMI_MAX_NUM_XCP; ++xcp)
    {
        for(size_t vcn = 0; vcn < AMDSMI_MAX_NUM_VCN; ++vcn)
        {
            EXPECT_EQ(collected_metrics.xcp_stats[xcp].vcn_busy[vcn],
                      static_cast<uint16_t>(xcp * 10 + vcn));
        }
    }
}

/**
 * Large Array Indices - JPEG Engines
 *
 * Objective: Verify no buffer overflow with maximum JPEG engines.
 */
TEST_F(DeviceTest, large_array_indices_jpeg)
{
    // Setup: Set all ROCPROFSYS_AMDSMI_JPEG_ENGINE_COUNT entries
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();

    for(size_t xcp = 0; xcp < AMDSMI_MAX_NUM_XCP; ++xcp)
    {
        for(size_t jpeg = 0; jpeg < ROCPROFSYS_AMDSMI_JPEG_ENGINE_COUNT; ++jpeg)
        {
            metrics.xcp_stats[xcp].jpeg_busy[jpeg] =
                static_cast<uint16_t>(xcp * 100 + jpeg);
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

    // Collect metrics - should not crash
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify all JPEG engines were processed correctly
    for(size_t xcp = 0; xcp < AMDSMI_MAX_NUM_XCP; ++xcp)
    {
        for(size_t jpeg = 0; jpeg < ROCPROFSYS_AMDSMI_JPEG_ENGINE_COUNT; ++jpeg)
        {
            EXPECT_EQ(collected_metrics.xcp_stats[xcp].jpeg_busy[jpeg],
                      static_cast<uint16_t>(xcp * 100 + jpeg));
        }
    }
}

/**
 * Concurrent Device Objects
 *
 * Objective: Verify multiple device objects don't interfere.
 */
TEST_F(DeviceTest, concurrent_device_objects)
{
    // Setup: Create mocks for two different devices
    auto mock_driver1 = std::make_shared<MockDriver>();
    auto mock_driver2 = std::make_shared<MockDriver>();

    amdsmi_processor_handle handle1 = reinterpret_cast<amdsmi_processor_handle>(0x1111);
    amdsmi_processor_handle handle2 = reinterpret_cast<amdsmi_processor_handle>(0x2222);

    // Device 1 returns power = 100W
    amdsmi_gpu_metrics_t metrics1 = CreateSentinelMetrics();
    metrics1.current_socket_power = 100;

    EXPECT_CALL(*mock_driver1, get_metrics_info(handle1, _))
        .Times(AtLeast(1))
        .WillRepeatedly(DoAll(SetArgPointee<1>(metrics1), Return(AMDSMI_STATUS_SUCCESS)));

    uint64_t sentinel_mem = 0xFFFFFFFFFFFFFFFFULL;
    EXPECT_CALL(*mock_driver1, get_memory_usage(handle1, AMDSMI_MEM_TYPE_VRAM, _))
        .Times(AtLeast(1))
        .WillRepeatedly(
            DoAll(SetArgPointee<2>(sentinel_mem), Return(AMDSMI_STATUS_SUCCESS)));

    SetupSDMAExpectations(mock_driver1, handle1);

    EXPECT_CALL(*mock_driver1, get_gpu_asic_info(handle1, _))
        .Times(AnyNumber())
        .WillRepeatedly(Return(AMDSMI_STATUS_SUCCESS));

    // Device 2 returns power = 200W
    amdsmi_gpu_metrics_t metrics2 = CreateSentinelMetrics();
    metrics2.current_socket_power = 200;

    EXPECT_CALL(*mock_driver2, get_metrics_info(handle2, _))
        .Times(AtLeast(1))
        .WillRepeatedly(DoAll(SetArgPointee<1>(metrics2), Return(AMDSMI_STATUS_SUCCESS)));

    EXPECT_CALL(*mock_driver2, get_memory_usage(handle2, AMDSMI_MEM_TYPE_VRAM, _))
        .Times(AtLeast(1))
        .WillRepeatedly(
            DoAll(SetArgPointee<2>(sentinel_mem), Return(AMDSMI_STATUS_SUCCESS)));

    SetupSDMAExpectations(mock_driver2, handle2);

    EXPECT_CALL(*mock_driver2, get_gpu_asic_info(handle2, _))
        .Times(AnyNumber())
        .WillRepeatedly(Return(AMDSMI_STATUS_SUCCESS));

    // Create two device objects
    device<MockDriver> dev1(mock_driver1, handle1, test_processor_type, 0);
    device<MockDriver> dev2(mock_driver2, handle2, test_processor_type, 1);

    // Collect from device 1
    auto result1 =
        dev1.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);
    EXPECT_EQ(result1.current_socket_power, 100U);

    // Collect from device 2
    auto result2 =
        dev2.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);
    EXPECT_EQ(result2.current_socket_power, 200U);

    // Collect from device 1 again - should still return 100W
    result1 = dev1.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);
    EXPECT_EQ(result1.current_socket_power, 100U);

    // Verify devices maintain independent state
    EXPECT_NE(dev1.get_index(), dev2.get_index());
}

/**
 * Device with Index 0
 *
 * Objective: Verify device index 0 works (boundary value).
 */
TEST_F(DeviceTest, device_with_index_zero)
{
    // Setup
    SetupAllMetricsSupported();

    // Create device with index 0
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, 0);

    // Verify index is correctly stored
    EXPECT_EQ(dev.get_index(), 0U);
}

/**
 * Device with High Index
 *
 * Objective: Verify device with high index works (multi-GPU scenario).
 */
TEST_F(DeviceTest, device_with_high_index)
{
    // Setup
    SetupAllMetricsSupported();

    // Create device with high index (simulating 16-GPU system)
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, 15);

    // Verify index is correctly stored
    EXPECT_EQ(dev.get_index(), 15U);
}

// ============================================================================
// Category 12: Integration Tests
// ============================================================================

/**
 * Full Lifecycle with Real-ish Data
 *
 * Objective: Simulate realistic GPU monitoring session with evolving metrics.
 */
TEST_F(DeviceTest, full_lifecycle_with_realistic_data)
{
    // Setup: Mock will return different values across collections
    auto mock = std::make_shared<MockDriver>();

    // Initialization metrics (used during device construction)
    amdsmi_gpu_metrics_t init_metrics = CreateSentinelMetrics();
    init_metrics.current_socket_power = 150;  // 150W
    init_metrics.temperature_hotspot  = 70;   // 70°C
    init_metrics.average_gfx_activity = 50;   // 50% activity

    // Collection 1: Idle GPU
    amdsmi_gpu_metrics_t metrics1 = CreateSentinelMetrics();
    metrics1.current_socket_power = 150;  // 150W
    metrics1.temperature_hotspot  = 70;   // 70°C
    metrics1.average_gfx_activity = 50;   // 50% activity

    // Collection 2: Heavy workload
    amdsmi_gpu_metrics_t metrics2 = CreateSentinelMetrics();
    metrics2.current_socket_power = 180;  // 180W
    metrics2.temperature_hotspot  = 75;   // 75°C
    metrics2.average_gfx_activity = 90;   // 90% activity

    // Collection 3: Returning to moderate
    amdsmi_gpu_metrics_t metrics3 = CreateSentinelMetrics();
    metrics3.current_socket_power = 160;  // 160W
    metrics3.temperature_hotspot  = 73;   // 73°C
    metrics3.average_gfx_activity = 60;   // 60% activity

    // Setup mock to return different values on each call
    // First call is during device construction (initialize_supported_metrics)
    // Subsequent calls are from get_gpu_metrics()
    EXPECT_CALL(*mock, get_metrics_info(test_handle, _))
        .WillOnce(DoAll(SetArgPointee<1>(init_metrics), Return(AMDSMI_STATUS_SUCCESS)))
        .WillOnce(DoAll(SetArgPointee<1>(metrics1), Return(AMDSMI_STATUS_SUCCESS)))
        .WillOnce(DoAll(SetArgPointee<1>(metrics2), Return(AMDSMI_STATUS_SUCCESS)))
        .WillOnce(DoAll(SetArgPointee<1>(metrics3), Return(AMDSMI_STATUS_SUCCESS)));

    uint64_t sentinel_mem = 0xFFFFFFFFFFFFFFFFULL;
    EXPECT_CALL(*mock, get_memory_usage(test_handle, AMDSMI_MEM_TYPE_VRAM, _))
        .Times(AtLeast(1))
        .WillRepeatedly(
            DoAll(SetArgPointee<2>(sentinel_mem), Return(AMDSMI_STATUS_SUCCESS)));

    SetupSDMAExpectations(mock, test_handle);

    EXPECT_CALL(*mock, get_gpu_asic_info(test_handle, _))
        .Times(AnyNumber())
        .WillRepeatedly(Return(AMDSMI_STATUS_SUCCESS));

    // Construct device
    device<MockDriver> dev(mock, test_handle, test_processor_type, test_index);

    // Collection 1: Idle
    auto result1 = dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);
    EXPECT_EQ(result1.current_socket_power, 150U);
    EXPECT_EQ(result1.hotspot_temperature, 70);
    EXPECT_EQ(result1.gfx_activity, 50U);

    // Collection 2: Heavy
    auto result2 = dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);
    EXPECT_EQ(result2.current_socket_power, 180U);
    EXPECT_EQ(result2.hotspot_temperature, 75);
    EXPECT_EQ(result2.gfx_activity, 90U);

    // Collection 3: Moderate
    auto result3 = dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);
    EXPECT_EQ(result3.current_socket_power, 160U);
    EXPECT_EQ(result3.hotspot_temperature, 73);
    EXPECT_EQ(result3.gfx_activity, 60U);
}

}  // namespace rocprofsys::pmc::collectors::gpu::testing
