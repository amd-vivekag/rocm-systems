/*************************************************************************
 * Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#include <gtest/gtest.h>
#include <hip/hip_runtime.h>
#include "MPITestBase.hpp"
#include "ResourceGuards.hpp"
#include "TestChecks.hpp"
#include "DeviceBufferHelpers.hpp"
#include "nccl.h"
#include "net.h"
#include <vector>
#include <memory>
#include <cstring>
#include <algorithm>

#ifdef MPI_TESTS_ENABLED

// Import helper namespaces
using namespace RCCLTestGuards;
using namespace RCCLTestHelpers;

// External NET IB plugin
extern ncclNet_t ncclNetIb;

// NET IB-specific resource deleters
struct NetMHandleDeleter {
    ncclNet_t* net;
    void* comm;

    NetMHandleDeleter(ncclNet_t* n = nullptr, void* c = nullptr) : net(n), comm(c) {}

    void operator()(void* mhandle) const {
        if (mhandle && net && comm) {
            int rank = MPIEnvironment::world_rank;
            TEST_INFO("Rank %d: NetMHandleDeleter - Deregistering memory handle (mhandle=%p, comm=%p)",
                      rank, mhandle, comm);
            ncclResult_t result = net->deregMr(comm, mhandle);
            TEST_INFO("Rank %d: NetMHandleDeleter - deregMr result: %d", rank, result);
        }
    }
};

// NET IB connection guard
class NetConnectionGuard {
private:
    ncclNet_t* net_;
    void* sendComm_;
    void* recvComm_;
    void* listenComm_;

public:
    explicit NetConnectionGuard(ncclNet_t* net)
        : net_(net), sendComm_(nullptr), recvComm_(nullptr), listenComm_(nullptr) {}

    ~NetConnectionGuard() {
        if (sendComm_ && net_) {
            net_->closeSend(sendComm_);
        }
        if (recvComm_ && net_) {
            net_->closeRecv(recvComm_);
        }
        if (listenComm_ && net_) {
            net_->closeListen(listenComm_);
        }
    }

    void setSendComm(void* comm) { sendComm_ = comm; }
    void setRecvComm(void* comm) { recvComm_ = comm; }
    void setListenComm(void* comm) { listenComm_ = comm; }

    NetConnectionGuard(const NetConnectionGuard&) = delete;
    NetConnectionGuard& operator=(const NetConnectionGuard&) = delete;
};

// Type alias for NetMHandleGuard using ResourceGuard
using NetMHandleGuard = RCCLTestGuards::ResourceGuard<void*, NetMHandleDeleter>;

// Test fixture for NET IB tests
class NetIbMPITest : public MPITestBase {
protected:
    static constexpr int kMinProcessesForMPI = 2;
    static constexpr bool kRequirePowerOfTwo = true;
    static constexpr int kNoNodeLimit = MPITestConstants::kNoNodeLimit;

    // Buffer pattern constants
    static constexpr int kBytePatternModulo = 256;

    // Timing constants
    static constexpr int kDefaultTimeoutMs = 5000;
    static constexpr int kLargeTransferTimeoutMs = 30000;
    static constexpr int kPollIntervalUs = 10000;  // 10ms
    static constexpr int kPollIntervalMs = 10;
    static constexpr int kMaxRetryAttempts = 1000;  // For NULL request handling

    // Buffer size constants
    static constexpr size_t kSmallBufferSize = 4096;
    static constexpr size_t kLargeBufferSize = 16 * 1024 * 1024;  // 16 MB

    // Test seed constants
    static constexpr int kBaseSeedOffset = 1000;
    static constexpr int kMultiSizeSeedOffset = 2000;

    // Debug output constants
    static constexpr int kNumDebugSamples = 4;

    // Invalid device ID offset for negative tests
    static constexpr int kInvalidDeviceOffset = 100;

    // Process count constants
    static constexpr int kExactTwoProcesses = 2;
    static constexpr int kMinGpusPerNode = 1;

    // Transfer test constants
    static constexpr int kNumSequentialTransfers = 100;
    static constexpr int kTransferTagBase = 300;

    // Timeout constants
    static constexpr int kLargeTransferTimeout = 30000;

    ncclNet_t* net_;
    int numDevices_;
    std::vector<int> deviceIds_;
    void* initCtx_;

    void SetUp() override {
        MPITestBase::SetUp();
        net_ = &ncclNetIb;
        numDevices_ = 0;
        initCtx_ = nullptr;
    }

    void TearDown() override {
        if (initCtx_) {
            net_->finalize(initCtx_);
            initCtx_ = nullptr;
        }
        MPITestBase::TearDown();
    }

    // Helper: Initialize NET IB plugin
    ncclResult_t InitNetIb() {
        ncclNetCommConfig_t commConfig = {};
        commConfig.trafficClass = NCCL_NET_TRAFFIC_CLASS_UNDEF;
        return net_->init(&initCtx_, 0, &commConfig, nullptr, nullptr);
    }

    // Helper: Get number of devices
    ncclResult_t GetDeviceCount(int* ndev) {
        return net_->devices(ndev);
    }

    // Helper: Get device properties
    ncclResult_t GetDeviceProperties(int dev, ncclNetProperties_t* props) {
        return net_->getProperties(dev, props);
    }

    // Helper: Create listen comm
    ncclResult_t CreateListenComm(int dev, ncclNetHandle_t* handle, void** listenComm) {
        return net_->listen(initCtx_, dev, handle, listenComm);
    }

    // Helper: Connect to remote
    ncclResult_t ConnectToRemote(int dev, ncclNetHandle_t* handle, void** sendComm) {
        return net_->connect(initCtx_, dev, handle, sendComm, nullptr);
    }

    // Helper: Accept connection
    ncclResult_t AcceptConnection(void* listenComm, void** recvComm) {
        return net_->accept(listenComm, recvComm, nullptr);
    }

    // Helper: Register memory
    ncclResult_t RegisterMemory(void* comm, void* data, size_t size, int type, void** mhandle) {
        return net_->regMr(comm, data, size, type, mhandle);
    }

    // Helper: Register DMA-BUF memory
    ncclResult_t RegisterDmaBufMemory(void* comm, void* data, size_t size, int type,
                                      uint64_t offset, int fd, void** mhandle) {
        return net_->regMrDmaBuf(comm, data, size, type, offset, fd, mhandle);
    }

    // Helper: Deregister memory
    ncclResult_t DeregisterMemory(void* comm, void* mhandle) {
        return net_->deregMr(comm, mhandle);
    }

    // Helper: Post send operation
    ncclResult_t PostSend(void* sendComm, void* data, size_t size, int tag,
                         void* mhandle, void** request) {
        return net_->isend(sendComm, data, size, tag, mhandle, nullptr, request);
    }

    // Helper: Post recv operation
    ncclResult_t PostRecv(void* recvComm, int n, void** data, size_t* sizes,
                         int* tags, void** mhandles, void** request) {
        return net_->irecv(recvComm, n, data, sizes, tags, mhandles, nullptr, request);
    }

    // Helper: Flush operation
    ncclResult_t FlushRecv(void* recvComm, int n, void** data, int* sizes,
                          void** mhandles, void** request) {
        return net_->iflush(recvComm, n, data, sizes, mhandles, request);
    }

    // Helper: Test request completion
    // No implementation for this method in the NET IB plugin
    ncclResult_t TestRequest(void* request, int* done, int* sizes) {
        return net_->test(request, done, sizes);
    }

    // Helper: Close send comm
    ncclResult_t CloseSendComm(void* sendComm) {
        return net_->closeSend(sendComm);
    }

    // Helper: Close recv comm
    ncclResult_t CloseRecvComm(void* recvComm) {
        return net_->closeRecv(recvComm);
    }

    // Helper: Close listen comm
    ncclResult_t CloseListenComm(void* listenComm) {
        return net_->closeListen(listenComm);
    }

    // Helper: Make virtual device
    ncclResult_t MakeVirtualDevice(int* dev, ncclNetVDeviceProps_t* props) {
        return net_->makeVDevice(dev, props);
    }

    // Helper: Setup connection between two ranks
    struct ConnectionPair {
        void* sendComm = nullptr;
        void* recvComm = nullptr;
        void* listenComm = nullptr;
        ncclNetHandle_t handle;
    };

    ncclResult_t SetupConnection(int dev, ConnectionPair& pair, int rank, int peerRank) {
        if (rank == 0) {
            // Rank 0: Listen
            RCCL_TEST_CHECK(CreateListenComm(dev, &pair.handle, &pair.listenComm));

            // Send handle to peer
            MPI_Send(&pair.handle, sizeof(ncclNetHandle_t), MPI_BYTE, peerRank, 0, MPI_COMM_WORLD);

            // Accept connection
            int done = 0;
            while (!done) {
                ncclResult_t result = AcceptConnection(pair.listenComm, &pair.recvComm);
                if (result == ncclSuccess && pair.recvComm != nullptr) {
                    done = 1;
                }
            }
        } else {
            // Rank 1: Connect
            MPI_Recv(&pair.handle, sizeof(ncclNetHandle_t), MPI_BYTE, peerRank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // Connect to peer
            int done = 0;
            while (!done) {
                ncclResult_t result = ConnectToRemote(dev, &pair.handle, &pair.sendComm);
                if (result == ncclSuccess && pair.sendComm != nullptr) {
                    done = 1;
                }
            }
        }

        MPI_Barrier(MPI_COMM_WORLD);
        return ncclSuccess;
    }

    // Helper: Initialize device buffer with pattern using DeviceBufferHelpers
    hipError_t InitializeBuffer(void* buffer, size_t size, int pattern) {
        // Use template-based helper with custom pattern: (pattern + i) % kBytePatternModulo
        return initializeBufferWithPattern<uint8_t>(
            buffer, size,
            [pattern](size_t i) { return static_cast<uint8_t>((pattern + i) % kBytePatternModulo); }
        );
    }

    // Helper: Verify device buffer pattern using DeviceBufferHelpers
    bool VerifyBuffer(void* buffer, size_t size, int pattern) {
        // Use template-based helper with pattern verification
        return verifyBufferData<uint8_t>(
            buffer, size,
            [pattern](size_t i) {
                return static_cast<uint8_t>((pattern + i) % kBytePatternModulo);
            }
        );
    }

    // Helper: Wait for request completion with timeout
    ncclResult_t WaitForCompletion(void* request, int* sizes, int timeoutMs = kDefaultTimeoutMs) {
        if (!request) return ncclInternalError;

        int done = 0;
        int attempts = 0;
        const int maxAttempts = timeoutMs / kPollIntervalMs;

        while (!done && attempts < maxAttempts) {
            ncclResult_t result = TestRequest(request, &done, sizes);

            if (result != ncclSuccess) {
                return result;
            }

            if (done) {
                break;
            } else {
                usleep(kPollIntervalUs); // 10ms
                attempts++;
            }
        }

        return done ? ncclSuccess : ncclInternalError;
    }
};

// Initialization Tests

TEST_F(NetIbMPITest, InitializePlugin) {
    ASSERT_TRUE(validateTestPrerequisites(kMinProcessesForMPI, MPITestConstants::kNoProcessLimit,
                                         kRequirePowerOfTwo, 1, kNoNodeLimit))
        << "Test requirements not met";

    ncclResult_t result = InitNetIb();
    ASSERT_EQ(result, ncclSuccess) << "Failed to initialize NET IB plugin";
    EXPECT_NE(initCtx_, nullptr) << "Init context should be set on success";
}

TEST_F(NetIbMPITest, GetDeviceCount) {
    ASSERT_TRUE(validateTestPrerequisites(kMinProcessesForMPI, MPITestConstants::kNoProcessLimit,
                                         kRequirePowerOfTwo, 1, kNoNodeLimit))
        << "Test requirements not met";

    ASSERT_EQ(InitNetIb(), ncclSuccess);

    int ndev = 0;
    EXPECT_EQ(GetDeviceCount(&ndev), ncclSuccess);
    EXPECT_GT(ndev, 0) << "No IB devices found";

    if (MPIEnvironment::world_rank == 0) {
        TEST_INFO("Found %d IB device(s)", ndev);
    }
}

// Device Properties Tests

TEST_F(NetIbMPITest, GetDeviceProperties) {
    ASSERT_TRUE(validateTestPrerequisites(kMinProcessesForMPI, MPITestConstants::kNoProcessLimit,
                                         kRequirePowerOfTwo, 1, kNoNodeLimit))
        << "Test requirements not met";

    ASSERT_EQ(InitNetIb(), ncclSuccess);

    int ndev = 0;
    ASSERT_EQ(GetDeviceCount(&ndev), ncclSuccess);
    ASSERT_GT(ndev, 0);

    for (int i = 0; i < ndev; i++) {
        ncclNetProperties_t props;
        memset(&props, 0, sizeof(props));

        EXPECT_EQ(GetDeviceProperties(i, &props), ncclSuccess)
            << "Failed to get properties for device " << i;

        // Verify properties are valid
        EXPECT_NE(props.name, nullptr) << "Device " << i << " has NULL name";
        EXPECT_GT(props.speed, 0) << "Device " << i << " has invalid speed";
        EXPECT_NE(props.pciPath, nullptr) << "Device " << i << " has NULL pciPath";

        if (MPIEnvironment::world_rank == 0) {
            TEST_INFO("Device %d: name=%s speed=%d pciPath=%s",
                   i, props.name, props.speed, props.pciPath);
        }
    }
}

TEST_F(NetIbMPITest, GetDevicePropertiesInvalidDevice) {
    ASSERT_TRUE(validateTestPrerequisites(kMinProcessesForMPI, MPITestConstants::kNoProcessLimit,
                                         kRequirePowerOfTwo, 1, kNoNodeLimit))
        << "Test requirements not met";

    ASSERT_EQ(InitNetIb(), ncclSuccess);

    int ndev = 0;
    ASSERT_EQ(GetDeviceCount(&ndev), ncclSuccess);

    ncclNetProperties_t props;

    // Invalid device ID (too large)
    ncclResult_t result = GetDeviceProperties(ndev + kInvalidDeviceOffset, &props);
    EXPECT_NE(result, ncclSuccess) << "Should fail for invalid device ID";
}

// Connection Setup Tests

TEST_F(NetIbMPITest, ListenAndConnect) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    ASSERT_EQ(InitNetIb(), ncclSuccess);

    int ndev = 0;
    ASSERT_EQ(GetDeviceCount(&ndev), ncclSuccess);
    ASSERT_GT(ndev, 0);

    ConnectionPair pair;
    int rank = MPIEnvironment::world_rank;
    int peerRank = (rank + 1) % 2;

    ASSERT_EQ(SetupConnection(0, pair, rank, peerRank), ncclSuccess);

    // Guard connections for automatic cleanup
    NetConnectionGuard connGuard(net_);
    if (rank == 0) {
        connGuard.setRecvComm(pair.recvComm);
        connGuard.setListenComm(pair.listenComm);
    } else {
        connGuard.setSendComm(pair.sendComm);
    }

    if (rank == 0) {
        EXPECT_NE(pair.recvComm, nullptr) << "Recv comm should be established";
        EXPECT_NE(pair.listenComm, nullptr) << "Listen comm should exist";
    } else {
        EXPECT_NE(pair.sendComm, nullptr) << "Send comm should be established";
    }
}

TEST_F(NetIbMPITest, MultipleSequentialConnections) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    ASSERT_EQ(InitNetIb(), ncclSuccess);

    int ndev = 0;
    ASSERT_EQ(GetDeviceCount(&ndev), ncclSuccess);
    ASSERT_GT(ndev, 0);

    int rank = MPIEnvironment::world_rank;
    int peerRank = (rank + 1) % 2;

    // Open and close multiple connections sequentially to test resource cleanup
    const int kNumIterations = 5;
    for (int iter = 0; iter < kNumIterations; iter++) {
        ConnectionPair pair;
        ASSERT_EQ(SetupConnection(0, pair, rank, peerRank), ncclSuccess)
            << "Failed to setup connection on iteration " << iter;

        NetConnectionGuard connGuard(net_);
        if (rank == 0) {
            connGuard.setRecvComm(pair.recvComm);
            connGuard.setListenComm(pair.listenComm);
            EXPECT_NE(pair.recvComm, nullptr);
        } else {
            connGuard.setSendComm(pair.sendComm);
            EXPECT_NE(pair.sendComm, nullptr);
        }

        MPI_Barrier(MPI_COMM_WORLD);
        // connGuard destructor closes connection
    }

    if (MPIEnvironment::world_rank == 0) {
        TEST_INFO("Successfully completed %d sequential connection cycles", kNumIterations);
    }
}

TEST_F(NetIbMPITest, ConnectWithInvalidHandle) {
    ASSERT_TRUE(validateTestPrerequisites(kMinProcessesForMPI, MPITestConstants::kNoProcessLimit,
                                         kRequirePowerOfTwo, 1, kNoNodeLimit))
        << "Test requirements not met";

    ASSERT_EQ(InitNetIb(), ncclSuccess);

    int ndev = 0;
    ASSERT_EQ(GetDeviceCount(&ndev), ncclSuccess);
    ASSERT_GT(ndev, 0);

    ncclNetHandle_t invalidHandle;
    memset(&invalidHandle, 0xFF, sizeof(invalidHandle));
    void* sendComm = nullptr;

    // Negative test: Connect with garbage handle
    ncclResult_t result = ConnectToRemote(0, &invalidHandle, &sendComm);
    EXPECT_EQ(result, ncclInternalError) << "Should fail with invalid handle";
}

// Memory Registration Tests

TEST_F(NetIbMPITest, RegisterHostMemory) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    ASSERT_EQ(InitNetIb(), ncclSuccess);

    int ndev = 0;
    ASSERT_EQ(GetDeviceCount(&ndev), ncclSuccess);
    ASSERT_GT(ndev, 0);

    ConnectionPair pair;
    int rank = MPIEnvironment::world_rank;
    int peerRank = (rank + 1) % 2;

    ASSERT_EQ(SetupConnection(0, pair, rank, peerRank), ncclSuccess);

    // Guard connections for automatic cleanup
    NetConnectionGuard connGuard(net_);
    if (rank == 0) {
        connGuard.setRecvComm(pair.recvComm);
        connGuard.setListenComm(pair.listenComm);
    } else {
        connGuard.setSendComm(pair.sendComm);
    }

    const size_t bufferSize = kSmallBufferSize;
    void* buffer = malloc(bufferSize);
    ASSERT_NE(buffer, nullptr);
    auto bufferGuard = makeHostBufferAutoGuard(buffer);

    void* mhandle = nullptr;
    void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;

    EXPECT_EQ(RegisterMemory(comm, buffer, bufferSize, NCCL_PTR_HOST, &mhandle), ncclSuccess);
    EXPECT_NE(mhandle, nullptr);

    // Use NetMHandleGuard for automatic deregistration before connection closes
    NetMHandleGuard mhandleGuard(mhandle, NetMHandleDeleter(net_, comm));
}

TEST_F(NetIbMPITest, RegisterGpuMemory) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    ASSERT_EQ(InitNetIb(), ncclSuccess);

    int ndev = 0;
    ASSERT_EQ(GetDeviceCount(&ndev), ncclSuccess);
    ASSERT_GT(ndev, 0);

    ConnectionPair pair;
    int rank = MPIEnvironment::world_rank;
    int peerRank = (rank + 1) % 2;

    ASSERT_EQ(SetupConnection(0, pair, rank, peerRank), ncclSuccess);

    // Guard connections for automatic cleanup
    NetConnectionGuard connGuard(net_);
    if (rank == 0) {
        connGuard.setRecvComm(pair.recvComm);
        connGuard.setListenComm(pair.listenComm);
    } else {
        connGuard.setSendComm(pair.sendComm);
    }

    const size_t bufferSize = kSmallBufferSize;
    void* buffer = nullptr;
    HIP_TEST_CHECK_GTEST_FAIL(hipMalloc(&buffer, bufferSize));
    auto bufferGuard = makeDeviceBufferAutoGuard(buffer);

    void* mhandle = nullptr;
    void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;

    EXPECT_EQ(RegisterMemory(comm, buffer, bufferSize, NCCL_PTR_CUDA, &mhandle), ncclSuccess);
    EXPECT_NE(mhandle, nullptr);

    // Use NetMHandleGuard for automatic deregistration before connection closes
    NetMHandleGuard mhandleGuard(mhandle, NetMHandleDeleter(net_, comm));
}

TEST_F(NetIbMPITest, RegisterMemoryNullPointer) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    ASSERT_EQ(InitNetIb(), ncclSuccess);

    int ndev = 0;
    ASSERT_EQ(GetDeviceCount(&ndev), ncclSuccess);
    ASSERT_GT(ndev, 0);

    ConnectionPair pair;
    int rank = MPIEnvironment::world_rank;
    int peerRank = (rank + 1) % 2;

    ASSERT_EQ(SetupConnection(0, pair, rank, peerRank), ncclSuccess);

    // Guard connections for automatic cleanup
    NetConnectionGuard connGuard(net_);
    if (rank == 0) {
        connGuard.setRecvComm(pair.recvComm);
        connGuard.setListenComm(pair.listenComm);
    } else {
        connGuard.setSendComm(pair.sendComm);
    }

    void* mhandle = nullptr;
    void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;

    // Negative test: NULL buffer pointer
    ncclResult_t result = RegisterMemory(comm, nullptr, 4096, NCCL_PTR_HOST, &mhandle);
    EXPECT_NE(result, ncclSuccess) << "Should fail with NULL buffer";
}

TEST_F(NetIbMPITest, DeregisterNullHandle) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    ASSERT_EQ(InitNetIb(), ncclSuccess);

    int ndev = 0;
    ASSERT_EQ(GetDeviceCount(&ndev), ncclSuccess);
    ASSERT_GT(ndev, 0);

    ConnectionPair pair;
    int rank = MPIEnvironment::world_rank;
    int peerRank = (rank + 1) % 2;

    ASSERT_EQ(SetupConnection(0, pair, rank, peerRank), ncclSuccess);

    // Guard connections for automatic cleanup
    NetConnectionGuard connGuard(net_);
    if (rank == 0) {
        connGuard.setRecvComm(pair.recvComm);
        connGuard.setListenComm(pair.listenComm);
    } else {
        connGuard.setSendComm(pair.sendComm);
    }

    void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;

    // Edge case: Deregister NULL handle (should be no-op)
    EXPECT_EQ(DeregisterMemory(comm, nullptr), ncclSuccess);
}

// Send/Recv Tests
TEST_F(NetIbMPITest, SimpleSendRecv) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    ASSERT_EQ(InitNetIb(), ncclSuccess);

    int ndev = 0;
    ASSERT_EQ(GetDeviceCount(&ndev), ncclSuccess);
    ASSERT_GT(ndev, 0);

    ConnectionPair pair;
    int rank = MPIEnvironment::world_rank;
    int peerRank = (rank + 1) % 2;

    ASSERT_EQ(SetupConnection(0, pair, rank, peerRank), ncclSuccess);

    // Guard connections for automatic cleanup
    NetConnectionGuard connGuard(net_);
    if (rank == 0) {
        connGuard.setRecvComm(pair.recvComm);
        connGuard.setListenComm(pair.listenComm);
    } else {
        connGuard.setSendComm(pair.sendComm);
    }

    const size_t bufferSize = kSmallBufferSize;
    const int tag = 42;

    void* buffer = malloc(bufferSize);
    ASSERT_NE(buffer, nullptr);
    auto bufferGuard = makeHostBufferAutoGuard(buffer);

    void* mhandle = nullptr;
    void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;
    ASSERT_EQ(RegisterMemory(comm, buffer, bufferSize, NCCL_PTR_HOST, &mhandle), ncclSuccess);

    // Use NetMHandleGuard for automatic cleanup on failure (exception safety)
    NetMHandleGuard mhandleGuard(mhandle, NetMHandleDeleter(net_, comm));

    void* request = nullptr;

    if (rank == 0) {
        // Receiver
        void* recvBuffers[1] = {buffer};
        size_t recvSizes[1] = {bufferSize};
        int recvTags[1] = {tag};
        void* recvHandles[1] = {mhandle};

        ASSERT_EQ(PostRecv(pair.recvComm, 1, recvBuffers, recvSizes, recvTags,
                          recvHandles, &request), ncclSuccess);
    } else {
        // Sender - initialize host buffer directly
        uint8_t* hostBuffer = static_cast<uint8_t*>(buffer);
        for (size_t i = 0; i < bufferSize; i++) {
            hostBuffer[i] = static_cast<uint8_t>((rank + i) % kBytePatternModulo);
        }

        // NET IB isend can return success with NULL request if FIFO isn't ready
        // (receiver's irecv RDMA write hasn't reached sender yet) — retry until ready
        int attempts = 0;
        do {
            ncclResult_t result = PostSend(pair.sendComm, buffer, bufferSize, tag, mhandle, &request);
            ASSERT_EQ(result, ncclSuccess);
            if (request != nullptr) break;
            if (++attempts >= kMaxRetryAttempts) {
                FAIL() << "PostSend returned NULL request after " << kMaxRetryAttempts << " attempts";
            }
            usleep(kPollIntervalUs);
        } while (request == nullptr);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    // Wait for completion
    int sizes[1] = {0};
    ASSERT_NE(request, nullptr) << "Request must be non-NULL before waiting";
    ASSERT_EQ(WaitForCompletion(request, sizes), ncclSuccess);

    if (rank == 0) {
        EXPECT_EQ(sizes[0], bufferSize) << "Received size mismatch";

        // Verify received data
        uint8_t* hostBuffer = static_cast<uint8_t*>(buffer);
        bool dataValid = true;
        int senderRank = 1;  // Data was sent by rank 1
        for (size_t i = 0; i < bufferSize && dataValid; i++) {
            uint8_t expected = static_cast<uint8_t>((senderRank + i) % kBytePatternModulo);
            if (hostBuffer[i] != expected) {
                dataValid = false;
            }
        }
        EXPECT_TRUE(dataValid) << "Data validation failed";
    }

    // NetMHandleGuard will automatically deregister memory when test scope ends
    // Destructor order ensures MR is deregistered before connection closes:
    //   1. mhandleGuard destructor (deregisters MR)
    //   2. bufferGuard destructor (frees buffer)
    //   3. connGuard destructor (closes connection)
}

TEST_F(NetIbMPITest, SendRecvMultipleSizes) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    int rank = MPIEnvironment::world_rank;

    ASSERT_EQ(InitNetIb(), ncclSuccess);

    int ndev = 0;
    ASSERT_EQ(GetDeviceCount(&ndev), ncclSuccess);
    ASSERT_GT(ndev, 0);

    ConnectionPair pair;
    int peerRank = (rank + 1) % 2;
    ASSERT_EQ(SetupConnection(0, pair, rank, peerRank), ncclSuccess);

    // Guard connections for automatic cleanup
    NetConnectionGuard connGuard(net_);
    if (rank == 0) {
        connGuard.setRecvComm(pair.recvComm);
        connGuard.setListenComm(pair.listenComm);
    } else {
        connGuard.setSendComm(pair.sendComm);
    }

    // Test various sizes
    std::vector<size_t> testSizes = {1, 64, 256, 1024, 4096, 16384, 65536};

    for (size_t size : testSizes) {
        const int tag = 100;
        const int seed = 2000 + static_cast<int>(size);  // Unique seed per size

        void* buffer = malloc(size);
        ASSERT_NE(buffer, nullptr);
        auto bufferGuard = makeHostBufferAutoGuard(buffer);  // Local guard for loop iteration

        void* mhandle = nullptr;
        void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;
        ASSERT_EQ(RegisterMemory(comm, buffer, size, NCCL_PTR_HOST, &mhandle), ncclSuccess);
        NetMHandleGuard mhandleGuard(mhandle, NetMHandleDeleter(net_, comm));

        void* request = nullptr;

        if (rank == 0) {
            memset(buffer, 0, size);

            void* recvBuffers[1] = {buffer};
            size_t recvSizes[1] = {size};
            int recvTags[1] = {tag};
            void* recvHandles[1] = {mhandle};

            ASSERT_EQ(PostRecv(pair.recvComm, 1, recvBuffers, recvSizes, recvTags,
                              recvHandles, &request), ncclSuccess);
            ASSERT_NE(request, nullptr) << "Recv request should never be NULL";
        } else {
            // Initialize host buffer directly (not using InitializeBuffer which expects device memory)
            uint8_t* hostBuffer = static_cast<uint8_t*>(buffer);
            for (size_t i = 0; i < size; i++) {
                hostBuffer[i] = static_cast<uint8_t>((seed + i) % kBytePatternModulo);
            }

            // NET IB isend can return success with NULL request if FIFO isn't ready
            // This means the receiver hasn't posted recv yet - retry until ready
            int attempts = 0;
            do {
                ncclResult_t result = PostSend(pair.sendComm, buffer, size, tag, mhandle, &request);
                ASSERT_EQ(result, ncclSuccess);

                if (request != nullptr) {
                    break;
                }

                // NULL request means "not ready yet", wait and retry
                if (++attempts >= kMaxRetryAttempts) {
                    FAIL() << "PostSend returned NULL request after " << kMaxRetryAttempts << " attempts";
                }
                usleep(kPollIntervalUs);
            } while (request == nullptr);
        }

        // Barrier 1: Ensure both ranks have posted their operations before waiting
        MPI_Barrier(MPI_COMM_WORLD);

        // Wait for completion
        int sizes[1] = {0};
        ASSERT_EQ(WaitForCompletion(request, sizes), ncclSuccess);

        // Barrier 2: CRITICAL - Ensure BOTH ranks have completed before EITHER continues
        // This prevents rank A from starting next transfer while rank B is still
        // completing current transfer, which would cause request object reuse race conditions
        MPI_Barrier(MPI_COMM_WORLD);

        if (rank == 0) {
            EXPECT_EQ(sizes[0], size) << "Size mismatch for transfer of " << size << " bytes";

            // Validate received data matches expected pattern (host buffer verification)
            uint8_t* hostBuffer = static_cast<uint8_t*>(buffer);
            bool dataValid = true;
            for (size_t j = 0; j < size && dataValid; j++) {
                uint8_t expected = static_cast<uint8_t>((seed + j) % kBytePatternModulo);
                if (hostBuffer[j] != expected) {
                    dataValid = false;
                }
            }
            EXPECT_TRUE(dataValid) << "Data validation failed for size " << size;
        }

        // NetMHandleGuard will automatically deregister at end of loop iteration
    }
}

TEST_F(NetIbMPITest, SendRecvZeroSize) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    ASSERT_EQ(InitNetIb(), ncclSuccess);

    int ndev = 0;
    ASSERT_EQ(GetDeviceCount(&ndev), ncclSuccess);
    ASSERT_GT(ndev, 0);

    ConnectionPair pair;
    int rank = MPIEnvironment::world_rank;
    int peerRank = (rank + 1) % 2;

    ASSERT_EQ(SetupConnection(0, pair, rank, peerRank), ncclSuccess);

    // Guard connections for automatic cleanup
    NetConnectionGuard connGuard(net_);
    if (rank == 0) {
        connGuard.setRecvComm(pair.recvComm);
        connGuard.setListenComm(pair.listenComm);
    } else {
        connGuard.setSendComm(pair.sendComm);
    }

    const size_t bufferSize = kSmallBufferSize;
    const int tag = 50;

    void* buffer = malloc(bufferSize);
    ASSERT_NE(buffer, nullptr);
    auto bufferGuard = makeHostBufferAutoGuard(buffer);

    void* mhandle = nullptr;
    void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;
    ASSERT_EQ(RegisterMemory(comm, buffer, bufferSize, NCCL_PTR_HOST, &mhandle), ncclSuccess);
    NetMHandleGuard mhandleGuard(mhandle, NetMHandleDeleter(net_, comm));

    void* request = nullptr;

    if (rank == 0) {
        // Receiver - expect zero bytes
        void* recvBuffers[1] = {buffer};
        size_t recvSizes[1] = {bufferSize};
        int recvTags[1] = {tag};
        void* recvHandles[1] = {mhandle};

        ASSERT_EQ(PostRecv(pair.recvComm, 1, recvBuffers, recvSizes, recvTags,
                          recvHandles, &request), ncclSuccess);
    } else {
        // Sender - send zero bytes (retry on NULL request — FIFO may not be ready)
        int attempts = 0;
        do {
            ncclResult_t result = PostSend(pair.sendComm, buffer, 0, tag, mhandle, &request);
            ASSERT_EQ(result, ncclSuccess);
            if (request != nullptr) break;
            if (++attempts >= kMaxRetryAttempts) {
                FAIL() << "PostSend returned NULL request after " << kMaxRetryAttempts << " attempts";
            }
            usleep(kPollIntervalUs);
        } while (request == nullptr);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    // Wait for completion
    int sizes[1] = {-1};
    ASSERT_NE(request, nullptr) << "Request must be non-NULL before waiting";
    ASSERT_EQ(WaitForCompletion(request, sizes), ncclSuccess);

    if (rank == 0) {
        EXPECT_EQ(sizes[0], 0) << "Should receive zero bytes";
    }
}

// =============================================================================
// Test: SendRecvDifferentMemoryTypes
//
// Creates two 4-NIC merged virtual devices from different physical NICs.
// Rank 0 (receiver) uses merged device A, Rank 1 (sender) uses merged device B.
// Tests all four memory type combinations: host→host, host→GPU, GPU→host, GPU→GPU.
//
// This exercises:
//   - Asymmetric merged device usage (different vNIC per rank)
//   - Multi-QP data path with 4 QPs per rank
//   - GDR path selection based on pointer type
//   - Memory registration across multiple Protection Domains
//   - Flush semantics for GPU memory receives
// =============================================================================
TEST_F(NetIbMPITest, SendRecvDifferentMemoryTypes) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    int rank = MPIEnvironment::world_rank;
    int peerRank = (rank + 1) % 2;

    // --- Init and discover devices ---
    ASSERT_EQ(InitNetIb(), ncclSuccess);
    int ndev = 0;
    ASSERT_EQ(GetDeviceCount(&ndev), ncclSuccess);
    ASSERT_GT(ndev, 0);

    std::vector<ncclNetProperties_t> props(ndev);
    std::vector<int> physDevs;
    for (int i = 0; i < ndev; i++) {
        memset(&props[i], 0, sizeof(ncclNetProperties_t));
        ASSERT_EQ(GetDeviceProperties(i, &props[i]), ncclSuccess);
        if (!props[i].name || !strchr(props[i].name, '+'))
            physDevs.push_back(i);
    }

    // --- Select 4 compatible devices per rank ---
    int needed = 8;
    if ((int)physDevs.size() < needed)
        GTEST_SKIP() << "Need " << needed << " physical devices, found " << physDevs.size();

    int targetSpeed = props[physDevs[0]].speed;
    std::vector<int> compat;
    for (int d : physDevs)
        if (props[d].speed == targetSpeed) compat.push_back(d);
    if ((int)compat.size() < needed)
        GTEST_SKIP() << "Need " << needed << " same-speed devices, found " << compat.size();

    // Rank 0: first 4, Rank 1: next 4
    int offset = (rank == 1) ? 4 : 0;
    std::vector<int> selected(compat.begin() + offset, compat.begin() + offset + 4);

    // --- Create merged device ---
    ncclNetVDeviceProps_t vProps;
    memset(&vProps, 0, sizeof(vProps));
    vProps.ndevs = 4;
    for (int i = 0; i < 4; i++) vProps.devs[i] = selected[i];

    int mergedDev = -1;
    ncclResult_t res = MakeVirtualDevice(&mergedDev, &vProps);
    if (res != ncclSuccess || mergedDev < 0)
        GTEST_SKIP() << "Rank " << rank << " failed to create merged device";

    ncclNetProperties_t mProps;
    memset(&mProps, 0, sizeof(mProps));
    ASSERT_EQ(GetDeviceProperties(mergedDev, &mProps), ncclSuccess);

    // --- Check GDR support across both ranks ---
    int localGdr = (mProps.ptrSupport & NCCL_PTR_CUDA) ? 1 : 0;
    int globalGdr = 0;
    MPI_Allreduce(&localGdr, &globalGdr, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
    bool gdr = (globalGdr == 1);

    // --- Test each memory type combination ---
    struct Combo { int send, recv; const char* desc; };
    Combo combos[] = {
        {NCCL_PTR_HOST, NCCL_PTR_HOST, "Host->Host"},
        {NCCL_PTR_HOST, NCCL_PTR_CUDA, "Host->GPU"},
        {NCCL_PTR_CUDA, NCCL_PTR_HOST, "GPU->Host"},
        {NCCL_PTR_CUDA, NCCL_PTR_CUDA, "GPU->GPU"},
    };

    for (auto& c : combos) {
        bool needGpu = (c.send == NCCL_PTR_CUDA || c.recv == NCCL_PTR_CUDA);
        if (needGpu && !gdr) {
            if (rank == 0) fprintf(stderr, "  [SKIP] %s (no GDR)\n", c.desc);
            MPI_Barrier(MPI_COMM_WORLD);
            continue;
        }

        int memType = (rank == 0) ? c.recv : c.send;

        // Connection
        ConnectionPair pair;
        ncclNetHandle_t handle;
        if (rank == 0) {
            ASSERT_EQ(CreateListenComm(mergedDev, &handle, &pair.listenComm), ncclSuccess);
            MPI_Send(&handle, sizeof(handle), MPI_BYTE, peerRank, 0, MPI_COMM_WORLD);
            while (!pair.recvComm) AcceptConnection(pair.listenComm, &pair.recvComm);
        } else {
            MPI_Recv(&handle, sizeof(handle), MPI_BYTE, peerRank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            while (!pair.sendComm) ConnectToRemote(mergedDev, &handle, &pair.sendComm);
        }
        MPI_Barrier(MPI_COMM_WORLD);

        NetConnectionGuard conn(net_);
        if (rank == 0) { conn.setRecvComm(pair.recvComm); conn.setListenComm(pair.listenComm); }
        else conn.setSendComm(pair.sendComm);

        // Buffer
        const size_t sz = kSmallBufferSize;
        void* buf = nullptr;
        if (memType == NCCL_PTR_CUDA) { HIP_TEST_CHECK_GTEST_FAIL(hipMalloc(&buf, sz)); }
        else { buf = malloc(sz); ASSERT_NE(buf, nullptr); }
        auto delBuf = [memType](void* ptr) {
            if (ptr) {
                if (memType == NCCL_PTR_CUDA) {
                    hipError_t err = hipFree(ptr);
                    if (err != hipSuccess) {
                        fprintf(stderr, "WARNING: hipFree failed: %s\n", hipGetErrorString(err));
                    }
                } else {
                    free(ptr);
                }
            }
        };
        std::unique_ptr<void, decltype(delBuf)> bufG(buf, delBuf);

        // Register
        void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;
        void* mh = nullptr;
        ASSERT_EQ(RegisterMemory(comm, buf, sz, memType, &mh), ncclSuccess);
        NetMHandleGuard mhG(mh, NetMHandleDeleter(net_, comm));

        // Fill / clear
        uint8_t seed = (uint8_t)(c.send * 10 + c.recv * 3 + 42);
        int tag = 700 + c.send * 2 + c.recv;
        if (rank == 1) {
            std::vector<uint8_t> pat(sz);
            for (size_t i = 0; i < sz; i++) pat[i] = (seed + i) % kBytePatternModulo;
            if (memType == NCCL_PTR_CUDA)
                HIP_TEST_CHECK_GTEST_FAIL(hipMemcpy(buf, pat.data(), sz, hipMemcpyHostToDevice));
            else memcpy(buf, pat.data(), sz);
        } else {
            if (memType == NCCL_PTR_CUDA) HIP_TEST_CHECK_GTEST_FAIL(hipMemset(buf, 0, sz));
            else memset(buf, 0, sz);
        }
        MPI_Barrier(MPI_COMM_WORLD);

        // Transfer
        void* req = nullptr;
        if (rank == 0) {
            void* b[1] = {buf}; size_t s[1] = {sz}; int t[1] = {tag}; void* h[1] = {mh};
            ASSERT_EQ(PostRecv(pair.recvComm, 1, b, s, t, h, &req), ncclSuccess);
            ASSERT_NE(req, nullptr);
        } else {
            for (int a = 0; !req && a < kMaxRetryAttempts; a++) {
                ASSERT_EQ(PostSend(pair.sendComm, buf, sz, tag, mh, &req), ncclSuccess);
                if (!req) usleep(kPollIntervalUs);
            }
            ASSERT_NE(req, nullptr) << "isend stuck NULL";
        }
        MPI_Barrier(MPI_COMM_WORLD);

        int csz[1] = {0};
        ASSERT_EQ(WaitForCompletion(req, csz), ncclSuccess);
        MPI_Barrier(MPI_COMM_WORLD);

        // Verify on receiver
        if (rank == 0) {
            EXPECT_EQ(csz[0], (int)sz);
            if (memType == NCCL_PTR_CUDA) {
                void* fb[1]={buf}; int fs[1]={(int)sz}; void* fh[1]={mh}; void* fr=nullptr;
                if (FlushRecv(pair.recvComm,1,fb,fs,fh,&fr)==ncclSuccess && fr)
                    ASSERT_EQ(WaitForCompletion(fr, nullptr), ncclSuccess);
            }
            std::vector<uint8_t> out(sz);
            if (memType == NCCL_PTR_CUDA)
                HIP_TEST_CHECK_GTEST_FAIL(hipMemcpy(out.data(), buf, sz, hipMemcpyDeviceToHost));
            else memcpy(out.data(), buf, sz);

            bool ok = true;
            for (size_t i = 0; i < sz && ok; i++)
                ok = (out[i] == (uint8_t)((seed + i) % kBytePatternModulo));
            EXPECT_TRUE(ok) << "Data mismatch for " << c.desc;
        }
        MPI_Barrier(MPI_COMM_WORLD);
    }
}

TEST_F(NetIbMPITest, SendRecvMultipleSizesFusion) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly kExactTwoProcesses processes";

    int rank = MPIEnvironment::world_rank;
    int peerRank = (rank + 1) % 2;

    ASSERT_EQ(InitNetIb(), ncclSuccess);
    int ndev = 0;
    ASSERT_EQ(GetDeviceCount(&ndev), ncclSuccess);
    ASSERT_GT(ndev, 0);

    std::vector<ncclNetProperties_t> props(ndev);
    std::vector<int> physDevs;
    for (int i = 0; i < ndev; i++) {
        memset(&props[i], 0, sizeof(ncclNetProperties_t));
        ASSERT_EQ(GetDeviceProperties(i, &props[i]), ncclSuccess);
        if (!props[i].name || !strchr(props[i].name, '+'))
            physDevs.push_back(i);
    }

    // Select 3 compatible devices per rank
    int needed = 6;
    if ((int)physDevs.size() < needed)
        GTEST_SKIP() << "Need " << needed << " physical devices, found " << physDevs.size();

    int targetSpeed = props[physDevs[0]].speed;
    std::vector<int> compat;
    for (int d : physDevs)
        if (props[d].speed == targetSpeed) compat.push_back(d);
    if ((int)compat.size() < needed)
        GTEST_SKIP() << "Need " << needed << " same-speed devices, found " << compat.size();

    int offset = (rank == 1) ? 3 : 0;
    std::vector<int> selected(compat.begin() + offset, compat.begin() + offset + 3);

    // Create 3-NIC merged device
    ncclNetVDeviceProps_t vProps;
    memset(&vProps, 0, sizeof(vProps));
    vProps.ndevs = 3;
    for (int i = 0; i < 3; i++) vProps.devs[i] = selected[i];

    int mergedDev = -1;
    ncclResult_t res = MakeVirtualDevice(&mergedDev, &vProps);
    if (res != ncclSuccess || mergedDev < 0)
        GTEST_SKIP() << "Rank " << rank << " failed to create 3-NIC merged device";

    // Build test size list
    long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) pageSize = 4096;

    std::vector<size_t> testSizes = {
        // Tiny — size < nqps (3 QPs → some get 0-byte chunks)
        1,
        // Non-power-of-two — uneven 3-way splits
        3, 7, 1023, 4097, 65537,
        // Page boundary
        (size_t)pageSize,
        // Powers of two: 2 bytes to 64 MB
        2, 4, 8, 16, 32, 64, 128, 256, 512, 1024,
        2048, 4096, 8192, 16384, 32768, 65536,
        128 * 1024, 256 * 1024, 512 * 1024,
        1024 * 1024,
        2 * 1024 * 1024,
        4 * 1024 * 1024,
        8 * 1024 * 1024,
        16 * 1024 * 1024,
        32 * 1024 * 1024,
        64 * 1024 * 1024,
    };

    // Sort and deduplicate (pageSize might duplicate 4096)
    std::sort(testSizes.begin(), testSizes.end());
    testSizes.erase(std::unique(testSizes.begin(), testSizes.end()), testSizes.end());

    size_t maxSize = testSizes.back();

    // Allocate max-size buffer once, register once
    void* buffer = malloc(maxSize);
    ASSERT_NE(buffer, nullptr);
    auto bufferGuard = makeHostBufferAutoGuard(buffer);

    // Setup connection through merged device
    ConnectionPair pair;
    ncclNetHandle_t handle;
    if (rank == 0) {
        ASSERT_EQ(CreateListenComm(mergedDev, &handle, &pair.listenComm), ncclSuccess);
        MPI_Send(&handle, sizeof(handle), MPI_BYTE, peerRank, 0, MPI_COMM_WORLD);
        while (!pair.recvComm) AcceptConnection(pair.listenComm, &pair.recvComm);
    } else {
        MPI_Recv(&handle, sizeof(handle), MPI_BYTE, peerRank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        while (!pair.sendComm) ConnectToRemote(mergedDev, &handle, &pair.sendComm);
    }
    MPI_Barrier(MPI_COMM_WORLD);

    NetConnectionGuard conn(net_);
    if (rank == 0) { conn.setRecvComm(pair.recvComm); conn.setListenComm(pair.listenComm); }
    else conn.setSendComm(pair.sendComm);

    void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;
    void* mhandle = nullptr;
    ASSERT_EQ(RegisterMemory(comm, buffer, maxSize, NCCL_PTR_HOST, &mhandle), ncclSuccess);
    NetMHandleGuard mhGuard(mhandle, NetMHandleDeleter(net_, comm));

    int passed = 0, failed = 0;

    for (size_t idx = 0; idx < testSizes.size(); idx++) {
        size_t sz = testSizes[idx];
        int tag = 800 + (int)idx;
        int seed = 9000 + (int)idx;

        if (rank == 1) {
            uint8_t* p = static_cast<uint8_t*>(buffer);
            for (size_t i = 0; i < sz; i++)
                p[i] = (uint8_t)((seed + i) % kBytePatternModulo);
        } else {
            memset(buffer, 0xDE, sz);
        }
        MPI_Barrier(MPI_COMM_WORLD);

        // Transfer
        void* req = nullptr;
        if (rank == 0) {
            void* b[1] = {buffer}; size_t s[1] = {sz}; int t[1] = {tag}; void* h[1] = {mhandle};
            ASSERT_EQ(PostRecv(pair.recvComm, 1, b, s, t, h, &req), ncclSuccess);
            ASSERT_NE(req, nullptr) << "Recv request NULL for size " << sz;
        } else {
            for (int a = 0; !req && a < kMaxRetryAttempts; a++) {
                ASSERT_EQ(PostSend(pair.sendComm, buffer, sz, tag, mhandle, &req), ncclSuccess);
                if (!req) usleep(kPollIntervalUs);
            }
            ASSERT_NE(req, nullptr) << "isend stuck NULL for size " << sz;
        }
        MPI_Barrier(MPI_COMM_WORLD);

        int csz[1] = {0};
        int timeout = (sz > 1024 * 1024) ? kLargeTransferTimeoutMs : kDefaultTimeoutMs;
        ASSERT_EQ(WaitForCompletion(req, csz, timeout), ncclSuccess)
            << "Transfer timeout for size " << sz;
        MPI_Barrier(MPI_COMM_WORLD);

        // Verify on receiver
        if (rank == 0) {
            bool sizeOk = (csz[0] == (int)sz);
            bool dataOk = true;
            size_t errIdx = 0;
            uint8_t errExp = 0, errGot = 0;

            if (sizeOk) {
                uint8_t* p = static_cast<uint8_t*>(buffer);
                for (size_t i = 0; i < sz; i++) {
                    uint8_t expected = (uint8_t)((seed + i) % kBytePatternModulo);
                    if (p[i] != expected) {
                        dataOk = false;
                        errIdx = i; errExp = expected; errGot = p[i];
                        break;
                    }
                }
            }

            bool ok = sizeOk && dataOk;
            if (ok) {
                passed++;
            } else {
                failed++;
                if (!sizeOk) {
                    fprintf(stderr, "  [FAIL] size=%10zu  size mismatch: got %d\n", sz, csz[0]);
                } else {
                    fprintf(stderr, "  [FAIL] size=%10zu  data error at byte %zu: "
                            "expected %u, got %u\n", sz, errIdx, errExp, errGot);
                }
            }
            fflush(stderr);

            EXPECT_TRUE(ok) << "Failed for size " << sz;
        }
        MPI_Barrier(MPI_COMM_WORLD);
    }
}

// Flush Tests

TEST_F(NetIbMPITest, FlushAfterRecv) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    ASSERT_EQ(InitNetIb(), ncclSuccess);

    int ndev = 0;
    ASSERT_EQ(GetDeviceCount(&ndev), ncclSuccess);
    ASSERT_GT(ndev, 0);

    // Check if GDR is available
    ncclNetProperties_t props;
    ASSERT_EQ(GetDeviceProperties(0, &props), ncclSuccess);

    if (!(props.ptrSupport & NCCL_PTR_CUDA)) {
        GTEST_SKIP() << "GDR not supported, skipping flush test";
    }

    ConnectionPair pair;
    int rank = MPIEnvironment::world_rank;
    int peerRank = (rank + 1) % 2;

    ASSERT_EQ(SetupConnection(0, pair, rank, peerRank), ncclSuccess);

    // Guard connections for automatic cleanup
    NetConnectionGuard connGuard(net_);
    if (rank == 0) {
        connGuard.setRecvComm(pair.recvComm);
        connGuard.setListenComm(pair.listenComm);
    } else {
        connGuard.setSendComm(pair.sendComm);
    }

    const size_t bufferSize = kSmallBufferSize;
    const int tag = 200;

    void* buffer = nullptr;
    HIP_TEST_CHECK_GTEST_FAIL(hipMalloc(&buffer, bufferSize));
    auto bufferGuard = makeDeviceBufferAutoGuard(buffer);  // false = device memory

    void* mhandle = nullptr;
    void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;
    ASSERT_EQ(RegisterMemory(comm, buffer, bufferSize, NCCL_PTR_CUDA, &mhandle), ncclSuccess);
    NetMHandleGuard mhandleGuard(mhandle, NetMHandleDeleter(net_, comm));

    void* request = nullptr;

    if (rank == 0) {
        // Receiver
        void* recvBuffers[1] = {buffer};
        size_t recvSizes[1] = {bufferSize};
        int recvTags[1] = {tag};
        void* recvHandles[1] = {mhandle};

        ASSERT_EQ(PostRecv(pair.recvComm, 1, recvBuffers, recvSizes, recvTags,
                          recvHandles, &request), ncclSuccess);
    } else {
        // Sender
        void* hostBuffer = malloc(bufferSize);
        ASSERT_NE(hostBuffer, nullptr);
        auto hostBufferGuard = makeHostBufferAutoGuard(hostBuffer);

        // Initialize host buffer directly (not using InitializeBuffer which expects device memory)
        uint8_t* hostBuf = static_cast<uint8_t*>(hostBuffer);
        for (size_t i = 0; i < bufferSize; i++) {
            hostBuf[i] = static_cast<uint8_t>((rank + i) % kBytePatternModulo);
        }
        HIP_TEST_CHECK_GTEST_FAIL(hipMemcpy(buffer, hostBuffer, bufferSize, hipMemcpyHostToDevice));

        int attempts = 0;
        do {
            ncclResult_t result = PostSend(pair.sendComm, buffer, bufferSize, tag, mhandle, &request);
            ASSERT_EQ(result, ncclSuccess);
            if (request != nullptr) break;
            if (++attempts >= kMaxRetryAttempts) {
                FAIL() << "PostSend returned NULL request after " << kMaxRetryAttempts << " attempts";
            }
            // usleep(kPollIntervalUs);
        } while (request == nullptr);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    // Wait for completion
    int sizes[1] = {0};
    ASSERT_NE(request, nullptr) << "Request must be non-NULL before waiting";
    ASSERT_EQ(WaitForCompletion(request, sizes), ncclSuccess);

    if (rank == 0) {
        // Issue flush
        void* flushBuffers[1] = {buffer};
        int flushSizes[1] = {static_cast<int>(bufferSize)};
        void* flushHandles[1] = {mhandle};
        void* flushRequest = nullptr;

        ncclResult_t result = FlushRecv(pair.recvComm, 1, flushBuffers, flushSizes,
                                       flushHandles, &flushRequest);

        if (result == ncclSuccess && flushRequest != nullptr) {
            int flushDone = 0;
            ASSERT_EQ(WaitForCompletion(flushRequest, nullptr), ncclSuccess);
        }
    }

    // NetMHandleGuard will automatically deregister at scope end
}

// Virtual Device Tests

TEST_F(NetIbMPITest, MakeVirtualDevice) {
    ASSERT_TRUE(validateTestPrerequisites(kMinProcessesForMPI, MPITestConstants::kNoProcessLimit,
                                         kRequirePowerOfTwo, 1, kNoNodeLimit))
        << "Test requirements not met";

    ASSERT_EQ(InitNetIb(), ncclSuccess);

    int ndev = 0;
    ASSERT_EQ(GetDeviceCount(&ndev), ncclSuccess);
    ASSERT_GT(ndev, 0);

    if (ndev < 2) {
        GTEST_SKIP() << "Need at least 2 devices for virtual device test";
    }

    ncclNetVDeviceProps_t vProps;
    vProps.ndevs = 2;
    vProps.devs[0] = 0;
    vProps.devs[1] = 1;

    int vdev = -1;
    ncclResult_t result = MakeVirtualDevice(&vdev, &vProps);

    // Virtual device creation may or may not be supported
    if (result == ncclSuccess) {
        EXPECT_GE(vdev, 0) << "Virtual device ID should be non-negative";

        if (MPIEnvironment::world_rank == 0) {
            TEST_INFO("Created virtual device %d from physical devices 0 and 1", vdev);
        }
    }
}

TEST_F(NetIbMPITest, MakeVirtualDeviceInvalidProps) {
    ASSERT_TRUE(validateTestPrerequisites(kMinProcessesForMPI, MPITestConstants::kNoProcessLimit,
                                         kRequirePowerOfTwo, 1, kNoNodeLimit))
        << "Test requirements not met";

    ASSERT_EQ(InitNetIb(), ncclSuccess);

    int ndev = 0;
    ASSERT_EQ(GetDeviceCount(&ndev), ncclSuccess);
    ASSERT_GT(ndev, 0);

    // Negative test: Zero devices
    ncclNetVDeviceProps_t vProps;
    vProps.ndevs = 0;

    int vdev = -1;
    ncclResult_t result = MakeVirtualDevice(&vdev, &vProps);
    EXPECT_EQ(result, ncclInvalidUsage) << "Should fail with zero devices";

}

// =============================================================================
// Test: MergeMultipleDevices
//
// Tests makeVDevice() with increasing numbers of physical devices:
//   - 3 devices: should succeed (within NCCL_NET_MAX_DEVS_PER_NIC = 4 limit)
//   - 4 devices: should succeed (at the limit)
//   - 5 devices: should fail (exceeds the limit)
//
// makeVDevice() is additive — each successful call appends a new merged device
// to the device list. Physical devices remain visible (hiding is done by the
// topology layer's XML manipulation, not by makeVDevice).
//
// =============================================================================
TEST_F(NetIbMPITest, MergeMultipleDevices) {
    ASSERT_TRUE(validateTestPrerequisites(kMinProcessesForMPI, MPITestConstants::kNoProcessLimit,
                                         kRequirePowerOfTwo, 1, kNoNodeLimit))
        << "Test requirements not met";

    ASSERT_EQ(InitNetIb(), ncclSuccess);

    int ndev = 0;
    ASSERT_EQ(GetDeviceCount(&ndev), ncclSuccess);
    ASSERT_GT(ndev, 0);

    // --- Collect all device properties and identify physical (non-merged) devices ---
    std::vector<ncclNetProperties_t> allProps(ndev);
    std::vector<int> physicalDevices;

    for (int i = 0; i < ndev; i++) {
        memset(&allProps[i], 0, sizeof(ncclNetProperties_t));
        ASSERT_EQ(GetDeviceProperties(i, &allProps[i]), ncclSuccess);

        bool isMerged = (allProps[i].name && strchr(allProps[i].name, '+') != nullptr);
        if (!isMerged) {
            physicalDevices.push_back(i);
        }
    }

    if (physicalDevices.size() < 5) {
        GTEST_SKIP() << "Need at least 5 physical (non-merged) IB devices, found "
                     << physicalDevices.size();
    }

    // --- Find 5 physical devices with matching speed ---
    int targetSpeed = allProps[physicalDevices[0]].speed;
    std::vector<int> selected;

    for (size_t i = 0; i < physicalDevices.size() && selected.size() < 5; i++) {
        int devIdx = physicalDevices[i];
        if (allProps[devIdx].speed == targetSpeed) {
            selected.push_back(devIdx);
        }
    }

    if (selected.size() < 5) {
        GTEST_SKIP() << "Could not find 5 physical devices with matching speed. "
                     << "Found " << selected.size() << " devices at speed " << targetSpeed;
    }

    // =========================================================================
    // Sub-test 1: Merge 3 devices (should succeed, under limit of 4)
    // =========================================================================
    {
        int ndevBefore = 0;
        ASSERT_EQ(GetDeviceCount(&ndevBefore), ncclSuccess);

        ncclNetVDeviceProps_t vProps;
        memset(&vProps, 0, sizeof(vProps));
        vProps.ndevs = 3;
        vProps.devs[0] = selected[0];
        vProps.devs[1] = selected[1];
        vProps.devs[2] = selected[2];

        int vdev = -1;
        ncclResult_t result = MakeVirtualDevice(&vdev, &vProps);

        EXPECT_EQ(result, ncclSuccess)
            << "Merging 3 devices should succeed (within limit of 4)";

        if (result == ncclSuccess) {
            EXPECT_GE(vdev, 0) << "Virtual device index should be non-negative";

            // Device count should increase by 1
            int ndevAfter = 0;
            ASSERT_EQ(GetDeviceCount(&ndevAfter), ncclSuccess);
            EXPECT_EQ(ndevAfter, ndevBefore + 1)
                << "makeVDevice should add exactly one device";

            // Verify the new merged device properties
            ncclNetProperties_t vdevProps;
            memset(&vdevProps, 0, sizeof(vdevProps));
            ASSERT_EQ(GetDeviceProperties(vdev, &vdevProps), ncclSuccess);

            // Name should contain two '+' separators (3 devices)
            ASSERT_NE(vdevProps.name, nullptr);
            std::string mergedName(vdevProps.name);
            int plusCount = 0;
            for (char c : mergedName) {
                if (c == '+') plusCount++;
            }
            EXPECT_EQ(plusCount, 2)
                << "3-device merge should have 2 '+' separators, got " << plusCount
                << " in name '" << mergedName << "'";

            // Speed should be sum of 3 constituents
            int expectedSpeed = allProps[selected[0]].speed +
                                allProps[selected[1]].speed +
                                allProps[selected[2]].speed;
            EXPECT_EQ(vdevProps.speed, expectedSpeed)
                << "Merged speed should be " << expectedSpeed << ", got " << vdevProps.speed;
        }
    }

    // =========================================================================
    // Sub-test 2: Merge 4 devices (should succeed, at the limit)
    // =========================================================================
    {
        int ndevBefore = 0;
        ASSERT_EQ(GetDeviceCount(&ndevBefore), ncclSuccess);

        ncclNetVDeviceProps_t vProps;
        memset(&vProps, 0, sizeof(vProps));
        vProps.ndevs = 4;
        vProps.devs[0] = selected[0];
        vProps.devs[1] = selected[1];
        vProps.devs[2] = selected[2];
        vProps.devs[3] = selected[3];

        int vdev = -1;
        ncclResult_t result = MakeVirtualDevice(&vdev, &vProps);

        EXPECT_EQ(result, ncclSuccess)
            << "Merging 4 devices should succeed (at the limit of NCCL_NET_MAX_DEVS_PER_NIC)";

        if (result == ncclSuccess) {
            EXPECT_GE(vdev, 0);

            int ndevAfter = 0;
            ASSERT_EQ(GetDeviceCount(&ndevAfter), ncclSuccess);
            EXPECT_EQ(ndevAfter, ndevBefore + 1);

            ncclNetProperties_t vdevProps;
            memset(&vdevProps, 0, sizeof(vdevProps));
            ASSERT_EQ(GetDeviceProperties(vdev, &vdevProps), ncclSuccess);

            // Name should contain three '+' separators (4 devices)
            ASSERT_NE(vdevProps.name, nullptr);
            std::string mergedName(vdevProps.name);
            int plusCount = 0;
            for (char c : mergedName) {
                if (c == '+') plusCount++;
            }
            EXPECT_EQ(plusCount, 3)
                << "4-device merge should have 3 '+' separators, got " << plusCount
                << " in name '" << mergedName << "'";

            // Speed should be sum of 4 constituents
            int expectedSpeed = allProps[selected[0]].speed +
                                allProps[selected[1]].speed +
                                allProps[selected[2]].speed +
                                allProps[selected[3]].speed;
            EXPECT_EQ(vdevProps.speed, expectedSpeed)
                << "Merged speed should be " << expectedSpeed << ", got " << vdevProps.speed;
        }
    }

    // =========================================================================
    // Sub-test 3: Merge 5 devices (should FAIL, exceeds limit of 4)
    // =========================================================================
    {
        int ndevBefore = 0;
        ASSERT_EQ(GetDeviceCount(&ndevBefore), ncclSuccess);

        ncclNetVDeviceProps_t vProps;
        memset(&vProps, 0, sizeof(vProps));
        vProps.ndevs = 5;
        vProps.devs[0] = selected[0];
        vProps.devs[1] = selected[1];
        vProps.devs[2] = selected[2];
        vProps.devs[3] = selected[3];
        vProps.devs[4] = selected[4];

        int vdev = -1;
        ncclResult_t result = MakeVirtualDevice(&vdev, &vProps);

        EXPECT_NE(result, ncclSuccess)
            << "Merging 5 devices should fail (exceeds NCCL_NET_MAX_DEVS_PER_NIC = 4)";

        // Device count should NOT have changed
        int ndevAfter = 0;
        ASSERT_EQ(GetDeviceCount(&ndevAfter), ncclSuccess);
        EXPECT_EQ(ndevAfter, ndevBefore)
            << "Failed merge should not add any devices";
    }
}

// Stress and Edge Case Tests

// Tests multiple sequential transfers on the same connection using host memory.
// This test validates that request objects can be properly reused across multiple
// send/recv operations without resource exhaustion or state corruption.
//
// SYNCHRONIZATION STRATEGY:
//   Two barriers per iteration ensure proper ordering:
//   1. After Post: Ensures both ranks have posted before either waits
//   2. After Completion: CRITICAL - Ensures BOTH ranks complete before EITHER
//      starts next iteration. Without this, rapid request reuse causes races.
//
// NULL REQUEST HANDLING:
//   NET IB isend() can return ncclSuccess with NULL request when the FIFO
//   isn't ready yet (receiver's irecv RDMA write hasn't reached sender yet).
//   This is NOT an error - it means "try again". The sender must retry until
//   it gets a valid request pointer. This is normal NET IB protocol behavior.
//
// NOTE: Flush (iflush) is intentionally NOT called because:
//   1. Flush is only needed for GPU Direct RDMA to ensure data visibility
//   2. For NCCL_PTR_HOST transfers, flush is unnecessary
TEST_F(NetIbMPITest, MultipleSequentialTransfers) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    int rank = MPIEnvironment::world_rank;

    ASSERT_EQ(InitNetIb(), ncclSuccess);

    int ndev = 0;
    ASSERT_EQ(GetDeviceCount(&ndev), ncclSuccess);
    ASSERT_GT(ndev, 0);

    ConnectionPair pair;
    int peerRank = (rank + 1) % 2;
    ASSERT_EQ(SetupConnection(0, pair, rank, peerRank), ncclSuccess);

    // Guard connections for automatic cleanup
    NetConnectionGuard connGuard(net_);
    if (rank == 0) {
        connGuard.setRecvComm(pair.recvComm);
        connGuard.setListenComm(pair.listenComm);
    } else {
        connGuard.setSendComm(pair.sendComm);
    }

    const size_t bufferSize = kSmallBufferSize;
    const int numTransfers = kNumSequentialTransfers;

    void* sendBuffer = nullptr;
    void* recvBuffer = nullptr;
    HostBufferAutoGuard sendBufferGuard(nullptr);
    HostBufferAutoGuard recvBufferGuard(nullptr);

    if (rank == 0) {
        recvBuffer = malloc(bufferSize);
        ASSERT_NE(recvBuffer, nullptr);
        recvBufferGuard = makeHostBufferAutoGuard(recvBuffer);
    } else {
        sendBuffer = malloc(bufferSize);
        ASSERT_NE(sendBuffer, nullptr);
        sendBufferGuard = makeHostBufferAutoGuard(sendBuffer);
    }

    void* mhandle = nullptr;
    void* buffer = (rank == 0) ? recvBuffer : sendBuffer;
    void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;
    ASSERT_EQ(RegisterMemory(comm, buffer, bufferSize, NCCL_PTR_HOST, &mhandle), ncclSuccess);
    NetMHandleGuard mhandleGuard(mhandle, NetMHandleDeleter(net_, comm));

    for (int i = 0; i < numTransfers; i++) {
        const int tag = kTransferTagBase + i;
        const int seed = kBaseSeedOffset + i;  // Unique seed for each transfer
        void* request = nullptr;

        if (rank == 0) {
            memset(recvBuffer, 0, bufferSize);

            void* recvBuffers[1] = {recvBuffer};
            size_t recvSizes[1] = {bufferSize};
            int recvTags[1] = {tag};
            void* recvHandles[1] = {mhandle};

            ASSERT_EQ(PostRecv(pair.recvComm, 1, recvBuffers, recvSizes, recvTags,
                              recvHandles, &request), ncclSuccess);
            ASSERT_NE(request, nullptr) << "Recv request should never be NULL";
        } else {
            // Initialize host buffer directly (not using InitializeBuffer which expects device memory)
            uint8_t* hostBuffer = static_cast<uint8_t*>(sendBuffer);
            for (size_t j = 0; j < bufferSize; j++) {
                hostBuffer[j] = static_cast<uint8_t>((seed + j) % kBytePatternModulo);
            }

            // NET IB isend can return success with NULL request if FIFO isn't ready
            // This means the receiver hasn't posted recv yet - retry until ready
            int attempts = 0;
            do {
                ncclResult_t result = PostSend(pair.sendComm, sendBuffer, bufferSize, tag, mhandle, &request);
                ASSERT_EQ(result, ncclSuccess);

                if (request != nullptr) {
                    break;
                }

                // NULL request means "not ready yet", wait and retry
                if (++attempts >= kMaxRetryAttempts) {
                    FAIL() << "PostSend returned NULL request after " << kMaxRetryAttempts << " attempts";
                }
                usleep(kPollIntervalUs);
            } while (request == nullptr);
        }

        // Barrier 1: Ensure both ranks have posted their operations before waiting
        MPI_Barrier(MPI_COMM_WORLD);

        // Wait for completion
        int sizes[1] = {0};
        ASSERT_EQ(WaitForCompletion(request, sizes), ncclSuccess);

        // Barrier 2: CRITICAL - Ensure BOTH ranks have completed before EITHER continues
        // This prevents rank A from starting transfer N+1 while rank B is still
        // completing transfer N, which would cause request object reuse race conditions
        MPI_Barrier(MPI_COMM_WORLD);

        if (rank == 0) {
            EXPECT_EQ(sizes[0], bufferSize) << "Transfer " << i << " size mismatch";

            // Validate received data matches expected pattern (host buffer verification)
            uint8_t* hostBuffer = static_cast<uint8_t*>(recvBuffer);
            bool dataValid = true;
            for (size_t j = 0; j < bufferSize && dataValid; j++) {
                uint8_t expected = static_cast<uint8_t>((seed + j) % kBytePatternModulo);
                if (hostBuffer[j] != expected) {
                    dataValid = false;
                }
            }
            EXPECT_TRUE(dataValid) << "Transfer " << i << " data validation failed (seed=" << seed << ")";

            if (!dataValid) {
                // Print first few mismatched values for debugging
                TEST_WARN("Rank %d: Transfer %d data mismatch. First %d values:", rank, i, kNumDebugSamples);
                for (size_t j = 0; j < kNumDebugSamples && j < bufferSize; j++) {
                    uint8_t expected = static_cast<uint8_t>((seed + j) % kBytePatternModulo);
                    TEST_WARN("  [%zu] expected=%u, got=%u %s",
                           j, expected, hostBuffer[j],
                           (hostBuffer[j] == expected) ? "PASS" : "FAIL");
                }
            }

            // NOTE: Flush is NOT called for host memory transfers
            // Flush (iflush) is only needed for GPU Direct RDMA to ensure data visibility on GPU.
            // For NCCL_PTR_HOST transfers, flush is unnecessary and calling it can cause
            // race conditions when request objects are rapidly reused.
            // The NET IB implementation will no-op the flush call for host memory anyway.
        }
    }

    // NetMHandleGuard will automatically deregister at scope end
}

TEST_F(NetIbMPITest, LargeTransfer) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    ASSERT_EQ(InitNetIb(), ncclSuccess);

    int ndev = 0;
    ASSERT_EQ(GetDeviceCount(&ndev), ncclSuccess);
    ASSERT_GT(ndev, 0);

    ConnectionPair pair;
    int rank = MPIEnvironment::world_rank;
    int peerRank = (rank + 1) % 2;

    ASSERT_EQ(SetupConnection(0, pair, rank, peerRank), ncclSuccess);

    // Guard connections for automatic cleanup
    NetConnectionGuard connGuard(net_);
    if (rank == 0) {
        connGuard.setRecvComm(pair.recvComm);
        connGuard.setListenComm(pair.listenComm);
    } else {
        connGuard.setSendComm(pair.sendComm);
    }

    const size_t bufferSize = kLargeBufferSize; // 16 MB
    const int tag = 400;

    void* buffer = malloc(bufferSize);
    ASSERT_NE(buffer, nullptr);
    auto bufferGuard = makeHostBufferAutoGuard(buffer);

    void* mhandle = nullptr;
    void* comm = (rank == 0) ? pair.recvComm : pair.sendComm;
    ASSERT_EQ(RegisterMemory(comm, buffer, bufferSize, NCCL_PTR_HOST, &mhandle), ncclSuccess);
    NetMHandleGuard mhandleGuard(mhandle, NetMHandleDeleter(net_, comm));

    void* request = nullptr;

    if (rank == 0) {
        // Receiver
        void* recvBuffers[1] = {buffer};
        size_t recvSizes[1] = {bufferSize};
        int recvTags[1] = {tag};
        void* recvHandles[1] = {mhandle};

        ASSERT_EQ(PostRecv(pair.recvComm, 1, recvBuffers, recvSizes, recvTags,
                          recvHandles, &request), ncclSuccess);
    } else {
        // Sender - Initialize host buffer directly (not using InitializeBuffer which expects device memory)
        uint8_t* hostBuffer = static_cast<uint8_t*>(buffer);
        for (size_t i = 0; i < bufferSize; i++) {
            hostBuffer[i] = static_cast<uint8_t>((rank + i) % kBytePatternModulo);
        }

        int attempts = 0;
        do {
            ncclResult_t result = PostSend(pair.sendComm, buffer, bufferSize, tag, mhandle, &request);
            ASSERT_EQ(result, ncclSuccess);
            if (request != nullptr) break;
            if (++attempts >= kMaxRetryAttempts) {
                FAIL() << "PostSend returned NULL request after " << kMaxRetryAttempts << " attempts";
            }
            usleep(kPollIntervalUs);
        } while (request == nullptr);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    // Wait for completion with longer timeout for large transfer
    int sizes[1] = {0};
    ASSERT_NE(request, nullptr) << "Request must be non-NULL before waiting";
    ASSERT_EQ(WaitForCompletion(request, sizes, kLargeTransferTimeout), ncclSuccess);

    if (rank == 0) {
        EXPECT_EQ(sizes[0], bufferSize) << "Large transfer size mismatch";

        // Verify received data
        uint8_t* hostBuffer = static_cast<uint8_t*>(buffer);
        bool dataValid = true;
        int senderRank = 1;  // Data was sent by rank 1
        size_t errorsFound = 0;
        const size_t maxErrorsToReport = 10;

        for (size_t i = 0; i < bufferSize && errorsFound < maxErrorsToReport; i++) {
            uint8_t expected = static_cast<uint8_t>((senderRank + i) % kBytePatternModulo);
            if (hostBuffer[i] != expected) {
                if (errorsFound == 0) {
                    TEST_WARN("Rank %d: Data validation errors found in large transfer:", rank);
                }
                TEST_WARN("  Index %zu: expected=%u, got=%u", i, expected, hostBuffer[i]);
                dataValid = false;
                errorsFound++;
            }
        }

        if (!dataValid && errorsFound >= maxErrorsToReport) {
            TEST_WARN("  ... (showing first %zu errors only)", maxErrorsToReport);
        }

        EXPECT_TRUE(dataValid) << "Large transfer data validation failed";
    }

    // NetMHandleGuard will automatically deregister at scope end
}

TEST_F(NetIbMPITest, CloseWithoutWaitingForCompletion) {
    ASSERT_TRUE(validateTestPrerequisites(kExactTwoProcesses, kExactTwoProcesses,
                                         false, kMinGpusPerNode, kNoNodeLimit))
        << "Test requires exactly " << kExactTwoProcesses << " processes";

    ASSERT_EQ(InitNetIb(), ncclSuccess);

    int ndev = 0;
    ASSERT_EQ(GetDeviceCount(&ndev), ncclSuccess);
    ASSERT_GT(ndev, 0);

    ConnectionPair pair;
    int rank = MPIEnvironment::world_rank;
    int peerRank = (rank + 1) % 2;

    ASSERT_EQ(SetupConnection(0, pair, rank, peerRank), ncclSuccess);

    // Guard connections for automatic cleanup
    NetConnectionGuard connGuard(net_);
    if (rank == 0) {
        connGuard.setRecvComm(pair.recvComm);
        connGuard.setListenComm(pair.listenComm);
    } else {
        connGuard.setSendComm(pair.sendComm);
    }
}

#endif // MPI_TESTS_ENABLED
