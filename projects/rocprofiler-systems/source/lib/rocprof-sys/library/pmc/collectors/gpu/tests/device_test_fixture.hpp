// Copyright (c) Advanced Micro Devices, Inc.
// SPDX-License-Identifier:  MIT

#pragma once

// Include amd_smi.hpp first to get proper AMD_SMI_SDMA_SUPPORTED detection
// based on the actual AMD SMI library version
#include "core/amd_smi.hpp"

#include "library/pmc/collectors/gpu/device.hpp"
#include "library/pmc/device_providers/amd_smi/drivers/tests/mock_driver.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>

using namespace rocprofsys::pmc::collectors::gpu;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;

using MockDriver =
    ::testing::StrictMock<rocprofsys::pmc::drivers::amd_smi::testing::mock_driver>;

namespace rocprofsys::pmc::collectors::gpu::testing
{

/**
 * @brief Test fixture for GPU device tests.
 *
 * Provides common setup for device tests including mock driver and
 * helper methods for configuring mock behavior.
 */
class DeviceTest : public ::testing::Test
{
protected:
    std::shared_ptr<MockDriver> mock_driver;
    amdsmi_processor_handle     test_handle;
    processor_type_t            test_processor_type;
    size_t                      test_index;

    void SetUp() override
    {
        mock_driver         = std::make_shared<MockDriver>();
        test_handle         = reinterpret_cast<amdsmi_processor_handle>(0x1234);
        test_processor_type = AMDSMI_PROCESSOR_TYPE_AMD_GPU;
        test_index          = 0;

        // Device info is always called during device initialization
        EXPECT_CALL(*mock_driver, get_gpu_asic_info(test_handle, _))
            .Times(AnyNumber())
            .WillRepeatedly(Return(AMDSMI_STATUS_SUCCESS));
    }

    /**
     * @brief Setup SDMA mock expectations for any device mock.
     * Call this for any mock that will have devices constructed with it.
     * No-op when SDMA is not supported.
     */
    template <typename MockPtr>
    static void SetupSDMAExpectations([[maybe_unused]] MockPtr&                mock,
                                      [[maybe_unused]] amdsmi_processor_handle handle)
    {
#if defined(AMD_SMI_SDMA_SUPPORTED) && AMD_SMI_SDMA_SUPPORTED == 1
        EXPECT_CALL(*mock, get_gpu_process_list(handle, _, _))
            .Times(AnyNumber())
            .WillRepeatedly(DoAll(SetArgPointee<1>(1), Return(AMDSMI_STATUS_SUCCESS)));
#endif
    }

    /**
     * @brief Configure mock to return GPU metrics with all valid values.
     */
    void SetupAllMetricsSupported()
    {
        amdsmi_gpu_metrics_t metrics = CreateValidMetrics();

        EXPECT_CALL(*mock_driver, get_metrics_info(test_handle, _))
            .Times(AtLeast(1))
            .WillRepeatedly(
                DoAll(SetArgPointee<1>(metrics), Return(AMDSMI_STATUS_SUCCESS)));

        uint64_t mem_usage = 8589934592ULL;  // 8 GB
        EXPECT_CALL(*mock_driver, get_memory_usage(test_handle, AMDSMI_MEM_TYPE_VRAM, _))
            .Times(AtLeast(1))
            .WillRepeatedly(
                DoAll(SetArgPointee<2>(mem_usage), Return(AMDSMI_STATUS_SUCCESS)));

        // SDMA support - allow any number of calls (happens during construction and
        // metrics collection)
#if defined(AMD_SMI_SDMA_SUPPORTED) && AMD_SMI_SDMA_SUPPORTED == 1
        EXPECT_CALL(*mock_driver, get_gpu_process_list(test_handle, _, _))
            .Times(AnyNumber())
            .WillRepeatedly(DoAll(SetArgPointee<1>(1), Return(AMDSMI_STATUS_SUCCESS)));
#endif
    }

    /**
     * @brief Configure mock to return GPU metrics with all sentinel values.
     */
    void SetupNoMetricsSupported()
    {
        amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();

        EXPECT_CALL(*mock_driver, get_metrics_info(test_handle, _))
            .Times(AtLeast(1))
            .WillRepeatedly(
                DoAll(SetArgPointee<1>(metrics), Return(AMDSMI_STATUS_SUCCESS)));

        uint64_t sentinel_mem = 0xFFFFFFFFFFFFFFFFULL;
        EXPECT_CALL(*mock_driver, get_memory_usage(test_handle, AMDSMI_MEM_TYPE_VRAM, _))
            .Times(AtLeast(1))
            .WillRepeatedly(
                DoAll(SetArgPointee<2>(sentinel_mem), Return(AMDSMI_STATUS_SUCCESS)));

        // SDMA support
#if defined(AMD_SMI_SDMA_SUPPORTED) && AMD_SMI_SDMA_SUPPORTED == 1
        EXPECT_CALL(*mock_driver, get_gpu_process_list(test_handle, _, _))
            .Times(AnyNumber())
            .WillRepeatedly(Return(AMDSMI_STATUS_NOT_SUPPORTED));
#endif
    }

    /**
     * @brief Configure mock to return GPU metrics with partial support.
     *
     * Returns valid values for:
     * - current_socket_power
     * - temperature_hotspot
     * - average_gfx_activity
     *
     * All other metrics return sentinel values.
     */
    void SetupPartialMetricsSupported()
    {
        amdsmi_gpu_metrics_t metrics = CreateSentinelMetrics();

        // Set specific metrics to valid values
        metrics.current_socket_power = 150;  // Valid power (watts)
        metrics.temperature_hotspot  = 75;   // Valid temp (degrees Celsius)
        metrics.average_gfx_activity = 85;   // Valid activity (percent)

        EXPECT_CALL(*mock_driver, get_metrics_info(test_handle, _))
            .Times(AtLeast(1))
            .WillRepeatedly(
                DoAll(SetArgPointee<1>(metrics), Return(AMDSMI_STATUS_SUCCESS)));

        constexpr uint64_t sentinel_mem = 0xFFFFFFFFFFFFFFFFULL;
        EXPECT_CALL(*mock_driver, get_memory_usage(test_handle, AMDSMI_MEM_TYPE_VRAM, _))
            .Times(AtLeast(1))
            .WillRepeatedly(
                DoAll(SetArgPointee<2>(sentinel_mem), Return(AMDSMI_STATUS_SUCCESS)));

        // SDMA support
#if defined(AMD_SMI_SDMA_SUPPORTED) && AMD_SMI_SDMA_SUPPORTED == 1
        EXPECT_CALL(*mock_driver, get_gpu_process_list(test_handle, _, _))
            .Times(AnyNumber())
            .WillRepeatedly(DoAll(SetArgPointee<1>(1), Return(AMDSMI_STATUS_SUCCESS)));
#endif
    }

    /**
     * @brief Create amdsmi_gpu_metrics_t with all valid (non-sentinel) values.
     */
    static amdsmi_gpu_metrics_t CreateValidMetrics()
    {
        amdsmi_gpu_metrics_t metrics{};

        // Power metrics
        metrics.current_socket_power = 150;
        metrics.average_socket_power = 140;

        // Temperature metrics (in millidegrees Celsius)
        metrics.temperature_hotspot = 75;
        metrics.temperature_edge    = 70;

        // Activity metrics (percentage)
        metrics.average_gfx_activity = 85;
        metrics.average_umc_activity = 60;
        metrics.average_mm_activity  = 40;

        // XCP stats - VCN and JPEG activity
        for(size_t xcp = 0; xcp < AMDSMI_MAX_NUM_XCP; ++xcp)
        {
            for(size_t i = 0; i < AMDSMI_MAX_NUM_VCN; ++i)
            {
                metrics.xcp_stats[xcp].vcn_busy[i] = static_cast<uint16_t>(50 + i);
            }
            for(size_t i = 0; i < ROCPROFSYS_AMDSMI_JPEG_ENGINE_COUNT; ++i)
            {
                metrics.xcp_stats[xcp].jpeg_busy[i] = static_cast<uint16_t>(30 + i);
            }
        }

        // XGMI metrics
        metrics.xgmi_link_width = 16;
        metrics.xgmi_link_speed = 25000;
        for(size_t i = 0; i < AMDSMI_MAX_NUM_XGMI_LINKS; ++i)
        {
            metrics.xgmi_read_data_acc[i]  = 1000000ULL + i;
            metrics.xgmi_write_data_acc[i] = 2000000ULL + i;
        }

        // PCIe metrics
        metrics.pcie_link_width     = 16;
        metrics.pcie_link_speed     = 16000;  // Gen4
        metrics.pcie_bandwidth_acc  = 500000000ULL;
        metrics.pcie_bandwidth_inst = 10000000ULL;

        return metrics;
    }

    /**
     * @brief Create amdsmi_gpu_metrics_t with all sentinel values.
     */
    static amdsmi_gpu_metrics_t CreateSentinelMetrics()
    {
        amdsmi_gpu_metrics_t metrics{};

        // uint16_t sentinel values
        metrics.current_socket_power = 0xFFFF;
        metrics.average_socket_power = 0xFFFF;
        metrics.average_gfx_activity = 0xFFFF;
        metrics.average_umc_activity = 0xFFFF;
        metrics.average_mm_activity  = 0xFFFF;

        // Temperature sentinel values (uint16_t fields)
        metrics.temperature_hotspot = 0xFFFF;
        metrics.temperature_edge    = 0xFFFF;

        // 16-bit sentinel for XCP stats
        for(size_t xcp = 0; xcp < AMDSMI_MAX_NUM_XCP; ++xcp)
        {
            for(size_t i = 0; i < AMDSMI_MAX_NUM_VCN; ++i)
            {
                metrics.xcp_stats[xcp].vcn_busy[i] = 0xFFFF;
            }
            for(size_t i = 0; i < ROCPROFSYS_AMDSMI_JPEG_ENGINE_COUNT; ++i)
            {
                metrics.xcp_stats[xcp].jpeg_busy[i] = 0xFFFF;
            }
        }

        // 16-bit sentinel for device-level VCN/JPEG activity arrays
        for(size_t i = 0; i < AMDSMI_MAX_NUM_VCN; ++i)
        {
            metrics.vcn_activity[i] = 0xFFFF;
        }
        for(size_t i = 0; i < AMDSMI_MAX_NUM_JPEG; ++i)
        {
            metrics.jpeg_activity[i] = 0xFFFF;
        }

        // 16-bit sentinel for XGMI link info
        metrics.xgmi_link_width = 0xFFFF;
        metrics.xgmi_link_speed = 0xFFFF;

        // 64-bit sentinel for XGMI data
        for(size_t i = 0; i < AMDSMI_MAX_NUM_XGMI_LINKS; ++i)
        {
            metrics.xgmi_read_data_acc[i]  = 0xFFFFFFFFFFFFFFFFULL;
            metrics.xgmi_write_data_acc[i] = 0xFFFFFFFFFFFFFFFFULL;
        }

        // 16-bit sentinel for PCIe link info
        metrics.pcie_link_width = 0xFFFF;
        metrics.pcie_link_speed = 0xFFFF;

        // 64-bit sentinel for PCIe bandwidth
        metrics.pcie_bandwidth_acc  = 0xFFFFFFFFFFFFFFFFULL;
        metrics.pcie_bandwidth_inst = 0xFFFFFFFFFFFFFFFFULL;

        return metrics;
    }
};

}  // namespace rocprofsys::pmc::collectors::gpu::testing
