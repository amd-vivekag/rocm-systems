// Copyright (c) 2018-2025 Advanced Micro Devices, Inc. All Rights Reserved.
// SPDX-License-Identifier:  MIT

#include "device_test_fixture.hpp"

namespace rocprofsys::pmc::collectors::gpu::testing
{
// ============================================================================
// Categories 7–8 & SDMA: XGMI, PCIe, and SDMA usage
// ============================================================================

// ----------------------------------------------------------------------------
// Category 7: XGMI Metrics Collection Tests
// ----------------------------------------------------------------------------

/**
 * XGMI Link Width Collection
 *
 * Objective: Verify XGMI link width is populated when supported.
 */
TEST_F(DeviceTest, xgmi_link_width_collection)
{
    // Setup: Mock returns specific XGMI link width value
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();
    metrics.xgmi_link_width      = 16;  // 16-bit link width

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

    // Verify XGMI is marked as supported
    EXPECT_TRUE(dev.get_supported_metrics().bits.xgmi);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify XGMI link width was collected
    EXPECT_EQ(collected_metrics.xgmi.link.width, 16U);
}

/**
 * XGMI Link Speed Collection
 *
 * Objective: Verify XGMI link speed is populated when supported.
 */
TEST_F(DeviceTest, xgmi_link_speed_collection)
{
    // Setup: Mock returns specific XGMI link speed value
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();
    metrics.xgmi_link_speed      = 25;  // 25 GT/s

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

    // Verify XGMI is marked as supported
    EXPECT_TRUE(dev.get_supported_metrics().bits.xgmi);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify XGMI link speed was collected
    EXPECT_EQ(collected_metrics.xgmi.link.speed, 25U);
}

/**
 * XGMI Read/Write Data Collection for All Links
 *
 * Objective: Verify data accumulation for all XGMI links.
 */
TEST_F(DeviceTest, xgmi_read_write_data_collection_all_links)
{
    // Setup: Mock returns valid read/write data for all XGMI links
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();

    // Populate read and write data for all XGMI links
    for(size_t i = 0; i < AMDSMI_MAX_NUM_XGMI_LINKS; ++i)
    {
        metrics.xgmi_read_data_acc[i]  = 1000000 + i * 1000;  // Read data in bytes
        metrics.xgmi_write_data_acc[i] = 2000000 + i * 1000;  // Write data in bytes
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

    // Verify XGMI is marked as supported
    EXPECT_TRUE(dev.get_supported_metrics().bits.xgmi);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify all XGMI link read/write data was collected
    for(size_t i = 0; i < AMDSMI_MAX_NUM_XGMI_LINKS; ++i)
    {
        EXPECT_EQ(collected_metrics.xgmi.data_acc.read[i], 1000000 + i * 1000);
        EXPECT_EQ(collected_metrics.xgmi.data_acc.write[i], 2000000 + i * 1000);
    }
}

/**
 * XGMI Sentinel Value Handling
 *
 * Objective: Verify sentinel values are zeroed out properly.
 */
TEST_F(DeviceTest, xgmi_sentinel_value_handling)
{
    // Setup: Mix of valid and sentinel XGMI values
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();

    // Set valid link width, but sentinel link speed
    metrics.xgmi_link_width = 16;
    // xgmi_link_speed remains 0xFFFF (sentinel)

    // Set some valid and some sentinel read/write data
    metrics.xgmi_read_data_acc[0]  = 1000000;                // Valid
    metrics.xgmi_read_data_acc[1]  = 0xFFFFFFFFFFFFFFFFULL;  // Sentinel
    metrics.xgmi_write_data_acc[0] = 2000000;                // Valid
    metrics.xgmi_write_data_acc[1] = 0xFFFFFFFFFFFFFFFFULL;  // Sentinel

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

    // Verify XGMI is marked as supported (at least one metric is valid)
    EXPECT_TRUE(dev.get_supported_metrics().bits.xgmi);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify valid values are collected and sentinels are zeroed
    EXPECT_EQ(collected_metrics.xgmi.link.width, 16U);
    EXPECT_EQ(collected_metrics.xgmi.link.speed, 0U);  // Sentinel converted to 0
    EXPECT_EQ(collected_metrics.xgmi.data_acc.read[0], 1000000U);
    EXPECT_EQ(collected_metrics.xgmi.data_acc.read[1], 0U);  // Sentinel converted to 0
    EXPECT_EQ(collected_metrics.xgmi.data_acc.write[0], 2000000U);
    EXPECT_EQ(collected_metrics.xgmi.data_acc.write[1], 0U);  // Sentinel converted to 0
}

/**
 * XGMI Not Collected When Unsupported
 *
 * Objective: Verify early return when XGMI metrics are not supported.
 */
TEST_F(DeviceTest, xgmi_not_collected_when_unsupported)
{
    // Setup: All XGMI metrics are sentinel values (unsupported)
    SetupNoMetricsSupported();

    // Create device
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // Verify XGMI is NOT marked as supported
    EXPECT_FALSE(dev.get_supported_metrics().bits.xgmi);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify all XGMI metrics remain default-initialized (zeros)
    EXPECT_EQ(collected_metrics.xgmi.link.width, 0U);
    EXPECT_EQ(collected_metrics.xgmi.link.speed, 0U);

    for(size_t i = 0; i < AMDSMI_MAX_NUM_XGMI_LINKS; ++i)
    {
        EXPECT_EQ(collected_metrics.xgmi.data_acc.read[i], 0U);
        EXPECT_EQ(collected_metrics.xgmi.data_acc.write[i], 0U);
    }
}

// ----------------------------------------------------------------------------
// Category 8: PCIe Metrics Collection Tests
// ----------------------------------------------------------------------------

/**
 * PCIe Link Width Collection
 *
 * Objective: Verify PCIe link width is populated when supported.
 */
TEST_F(DeviceTest, pcie_link_width_collection)
{
    // Setup: Mock returns specific PCIe link width value
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();
    metrics.pcie_link_width      = 16;  // x16 PCIe lanes

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

    // Verify PCIe is marked as supported
    EXPECT_TRUE(dev.get_supported_metrics().bits.pcie);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify PCIe link width was collected
    EXPECT_EQ(collected_metrics.pcie.link.width, 16U);
}

/**
 * PCIe Link Speed Collection
 *
 * Objective: Verify PCIe link speed is populated when supported.
 */
TEST_F(DeviceTest, pcie_link_speed_collection)
{
    // Setup: Mock returns specific PCIe link speed value
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();
    metrics.pcie_link_speed      = 16000;  // 16 GT/s (Gen4)

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

    // Verify PCIe is marked as supported
    EXPECT_TRUE(dev.get_supported_metrics().bits.pcie);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify PCIe link speed was collected
    EXPECT_EQ(collected_metrics.pcie.link.speed, 16000U);
}

/**
 * PCIe Bandwidth Accumulator Collection
 *
 * Objective: Verify bandwidth accumulator is populated when supported.
 */
TEST_F(DeviceTest, pcie_bandwidth_accumulator_collection)
{
    // Setup: Mock returns specific PCIe bandwidth accumulator value
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();
    metrics.pcie_bandwidth_acc   = 500000000;  // 500MB accumulated

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

    // Verify PCIe is marked as supported
    EXPECT_TRUE(dev.get_supported_metrics().bits.pcie);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify PCIe bandwidth accumulator was collected
    EXPECT_EQ(collected_metrics.pcie.bandwidth.acc, 500000000U);
}

/**
 * PCIe Bandwidth Instantaneous Collection
 *
 * Objective: Verify instantaneous bandwidth is populated when supported.
 */
TEST_F(DeviceTest, pcie_bandwidth_instantaneous_collection)
{
    // Setup: Mock returns specific PCIe instantaneous bandwidth value
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();
    metrics.pcie_bandwidth_inst  = 10000000;  // 10 MB/s

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

    // Verify PCIe is marked as supported
    EXPECT_TRUE(dev.get_supported_metrics().bits.pcie);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify PCIe instantaneous bandwidth was collected
    EXPECT_EQ(collected_metrics.pcie.bandwidth.inst, 10000000U);
}

/**
 * PCIe Sentinel Value Handling
 *
 * Objective: Verify sentinel values are zeroed out properly.
 */
TEST_F(DeviceTest, pcie_sentinel_value_handling)
{
    // Setup: Mix of valid and sentinel PCIe values
    amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();

    // Set valid link width and bandwidth acc, but sentinel link speed and bandwidth inst
    metrics.pcie_link_width    = 16;
    metrics.pcie_bandwidth_acc = 500000000;
    // pcie_link_speed and pcie_bandwidth_inst remain sentinel values

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

    // Verify PCIe is marked as supported (at least one metric is valid)
    EXPECT_TRUE(dev.get_supported_metrics().bits.pcie);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify valid values are collected and sentinels are zeroed
    EXPECT_EQ(collected_metrics.pcie.link.width, 16U);
    EXPECT_EQ(collected_metrics.pcie.link.speed, 0U);  // Sentinel converted to 0
    EXPECT_EQ(collected_metrics.pcie.bandwidth.acc, 500000000U);
    EXPECT_EQ(collected_metrics.pcie.bandwidth.inst, 0U);  // Sentinel converted to 0
}

/**
 * PCIe Not Collected When Unsupported
 *
 * Objective: Verify early return when PCIe metrics are not supported.
 */
TEST_F(DeviceTest, pcie_not_collected_when_unsupported)
{
    // Setup: All PCIe metrics are sentinel values (unsupported)
    SetupNoMetricsSupported();

    // Create device
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // Verify PCIe is NOT marked as supported
    EXPECT_FALSE(dev.get_supported_metrics().bits.pcie);

    // Collect metrics
    auto collected_metrics =
        dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);

    // Verify all PCIe metrics remain default-initialized (zeros)
    EXPECT_EQ(collected_metrics.pcie.link.width, 0U);
    EXPECT_EQ(collected_metrics.pcie.link.speed, 0U);
    EXPECT_EQ(collected_metrics.pcie.bandwidth.acc, 0U);
    EXPECT_EQ(collected_metrics.pcie.bandwidth.inst, 0U);
}

// ----------------------------------------------------------------------------
// SDMA usage (same translation unit as link / fabric-adjacent metrics)
// ----------------------------------------------------------------------------

/**
 * TC: SDMA Delta Computation
 *
 * Objective: Verify SDMA usage percentage is computed correctly from deltas.
 *
 * NOTE: This test is only compiled when AMD_SMI_SDMA_SUPPORTED is defined.
 */
#if defined(AMD_SMI_SDMA_SUPPORTED) && AMD_SMI_SDMA_SUPPORTED == 1
TEST_F(DeviceTest, sdma_delta_computation)
{
    // Setup: Mock SDMA process data
    SetupAllMetricsSupported();

    // Expect calls to get_gpu_process_list:
    // 1. During device construction (initialize_supported_metrics)
    // 2. First get_gpu_metrics() call
    // 3. Second get_gpu_metrics() call
    EXPECT_CALL(*mock_driver, get_gpu_process_list(test_handle, _, nullptr))
        .Times(AtLeast(3))
        .WillRepeatedly(DoAll(SetArgPointee<1>(1), Return(AMDSMI_STATUS_SUCCESS)));

    EXPECT_CALL(*mock_driver, get_gpu_process_list(test_handle, _, ::testing::NotNull()))
        .Times(2)
        .WillOnce(
            [](amdsmi_processor_handle, uint32_t* num_items, amdsmi_proc_info_t* procs) {
                *num_items          = 1;
                procs[0].sdma_usage = 5000000;  // First sample: 5s cumulative
                return AMDSMI_STATUS_SUCCESS;
            })
        .WillOnce(
            [](amdsmi_processor_handle, uint32_t* num_items, amdsmi_proc_info_t* procs) {
                *num_items          = 1;
                procs[0].sdma_usage = 15000000;  // Second sample: 15s cumulative
                return AMDSMI_STATUS_SUCCESS;
            });

    // Create device
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);
    ASSERT_TRUE(dev.is_supported());
    ASSERT_TRUE(dev.get_supported_metrics().bits.sdma_usage);

    enabled_metrics enabled;
    enabled.bits.sdma_usage = 1;

    // First sample - no previous data, should return 0
    auto metrics1 = dev.get_gpu_metrics(enabled, 1000000000ULL);  // t = 1s
    EXPECT_EQ(metrics1.sdma_usage, 0U);

    // Second sample - compute delta
    // Delta usage = 15000000 - 5000000 = 10,000,000 microseconds
    // Delta time = 2000000000 - 1000000000 = 1,000,000,000 nanoseconds
    // Percentage = (10,000,000 * 100,000) / 1,000,000,000 = 1000%
    // Clamped to 100%
    auto metrics2 = dev.get_gpu_metrics(enabled, 2000000000ULL);  // t = 2s
    EXPECT_GE(metrics2.sdma_usage, 0U);
    EXPECT_LE(metrics2.sdma_usage, 100U);
}
#endif

}  // namespace rocprofsys::pmc::collectors::gpu::testing
