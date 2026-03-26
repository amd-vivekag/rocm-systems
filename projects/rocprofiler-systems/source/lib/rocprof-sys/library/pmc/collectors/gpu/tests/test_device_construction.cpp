// Copyright (c) 2018-2025 Advanced Micro Devices, Inc. All Rights Reserved.
// SPDX-License-Identifier:  MIT

#include "device_test_fixture.hpp"

namespace rocprofsys::pmc::collectors::gpu::testing
{
// ============================================================================
// Category 1: Constructor and Initialization Tests
// ============================================================================

/**
 * TC1.1: Valid Device Construction with Full Metric Support
 *
 * Objective: Verify device initializes correctly when all metrics are supported.
 */
TEST_F(DeviceTest, valid_device_construction_full_support)
{
    // Setup: All metrics return valid values
    SetupAllMetricsSupported();

    // Create device
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // Verify device is supported
    EXPECT_TRUE(dev.is_supported());

    // Verify all metric bits are set
    auto supported = dev.get_supported_metrics();
    EXPECT_NE(supported.value, 0U);

    // Verify basic properties
    EXPECT_EQ(dev.get_index(), test_index);
}

/**
 * TC1.2: Device Construction with No Supported Metrics
 *
 * Objective: Verify device handles hardware with no supported metrics.
 */
TEST_F(DeviceTest, device_construction_no_support)
{
    // Setup: All metrics return sentinel values
    SetupNoMetricsSupported();

    // Create device
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // Verify device is not supported
    EXPECT_FALSE(dev.is_supported());

    // Verify no metric bits are set
    auto supported = dev.get_supported_metrics();
    EXPECT_EQ(supported.value, 0U);

    // Verify get_gpu_metrics returns all zeros
    auto metrics = dev.get_gpu_metrics(enabled_metrics{ .value = 0xFFFF }, 1000000000ULL);
    EXPECT_EQ(metrics.current_socket_power, 0U);
    EXPECT_EQ(metrics.average_socket_power, 0U);
    EXPECT_EQ(metrics.memory_usage, 0ULL);
}

/**
 * TC1.3: Device Construction with Partial Metric Support
 *
 * Objective: Verify selective metric initialization.
 */
TEST_F(DeviceTest, device_construction_partial_support)
{
    // Setup: Only specific metrics supported
    SetupPartialMetricsSupported();

    // Create device
    device<MockDriver> dev(mock_driver, test_handle, test_processor_type, test_index);

    // Verify device is supported (at least one metric available)
    EXPECT_TRUE(dev.is_supported());

    // Verify only expected metrics are marked as supported
    auto supported = dev.get_supported_metrics();

    EXPECT_TRUE(supported.bits.current_socket_power);
    EXPECT_TRUE(supported.bits.hotspot_temperature);
    EXPECT_TRUE(supported.bits.gfx_activity);

    // Verify unsupported metrics are not set
    EXPECT_FALSE(supported.bits.average_socket_power);
    EXPECT_FALSE(supported.bits.edge_temperature);
    EXPECT_FALSE(supported.bits.umc_activity);
    EXPECT_FALSE(supported.bits.mm_activity);
    EXPECT_FALSE(supported.bits.memory_usage);
    EXPECT_FALSE(supported.bits.vcn_activity);
    EXPECT_FALSE(supported.bits.jpeg_activity);
    EXPECT_FALSE(supported.bits.vcn_busy);
    EXPECT_FALSE(supported.bits.jpeg_busy);
    EXPECT_FALSE(supported.bits.xgmi);
    EXPECT_FALSE(supported.bits.pcie);
}

/**
 * TC1.4: Device Construction with Different Indices
 *
 * Objective: Verify device index is correctly stored for different device instances.
 */
TEST_F(DeviceTest, device_construction_different_indices)
{
    SetupAllMetricsSupported();

    // Test with different indices
    {
        device<MockDriver> dev(mock_driver, test_handle, AMDSMI_PROCESSOR_TYPE_AMD_GPU,
                               0);
        EXPECT_EQ(dev.get_index(), 0U);
    }

    {
        device<MockDriver> dev(mock_driver, test_handle, AMDSMI_PROCESSOR_TYPE_AMD_GPU,
                               1);
        EXPECT_EQ(dev.get_index(), 1U);
    }

    {
        device<MockDriver> dev(mock_driver, test_handle, AMDSMI_PROCESSOR_TYPE_AMD_GPU,
                               2);
        EXPECT_EQ(dev.get_index(), 2U);
    }
}

}  // namespace rocprofsys::pmc::collectors::gpu::testing
