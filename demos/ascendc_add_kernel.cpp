// Derived from Huawei Ascend Samples, AddKernelInvocationAcl/add_custom.cpp.
// Copyright (C) 2024 Huawei Technologies Co., Ltd. All rights reserved.
// Modified for a standalone CANN 9 demonstration with self-generated inputs.
// See licenses/Apache-2.0-Ascend-samples.txt and THIRD_PARTY_NOTICES.md.

#include "kernel_operator.h"

namespace {
constexpr int32_t TOTAL_LENGTH = 8 * 2048;
constexpr int32_t BLOCK_DIM = 8;
constexpr int32_t BLOCK_LENGTH = TOTAL_LENGTH / BLOCK_DIM;
constexpr int32_t TILE_NUM = 8;
constexpr int32_t BUFFER_NUM = 2;
constexpr int32_t TILE_LENGTH = BLOCK_LENGTH / TILE_NUM / BUFFER_NUM;
}

class KernelAdd {
public:
    __aicore__ inline void Init(GM_ADDR x, GM_ADDR y, GM_ADDR z)
    {
        xGlobal.SetGlobalBuffer((__gm__ half *)x + BLOCK_LENGTH * AscendC::GetBlockIdx(),
                                BLOCK_LENGTH);
        yGlobal.SetGlobalBuffer((__gm__ half *)y + BLOCK_LENGTH * AscendC::GetBlockIdx(),
                                BLOCK_LENGTH);
        zGlobal.SetGlobalBuffer((__gm__ half *)z + BLOCK_LENGTH * AscendC::GetBlockIdx(),
                                BLOCK_LENGTH);
        pipe.InitBuffer(xQueue, BUFFER_NUM, TILE_LENGTH * sizeof(half));
        pipe.InitBuffer(yQueue, BUFFER_NUM, TILE_LENGTH * sizeof(half));
        pipe.InitBuffer(zQueue, BUFFER_NUM, TILE_LENGTH * sizeof(half));
    }

    __aicore__ inline void Process()
    {
        for (int32_t tile = 0; tile < TILE_NUM * BUFFER_NUM; ++tile) {
            CopyIn(tile);
            Compute();
            CopyOut(tile);
        }
    }

private:
    __aicore__ inline void CopyIn(int32_t tile)
    {
        AscendC::LocalTensor<half> xLocal = xQueue.AllocTensor<half>();
        AscendC::LocalTensor<half> yLocal = yQueue.AllocTensor<half>();
        AscendC::DataCopy(xLocal, xGlobal[tile * TILE_LENGTH], TILE_LENGTH);
        AscendC::DataCopy(yLocal, yGlobal[tile * TILE_LENGTH], TILE_LENGTH);
        xQueue.EnQue(xLocal);
        yQueue.EnQue(yLocal);
    }

    __aicore__ inline void Compute()
    {
        AscendC::LocalTensor<half> xLocal = xQueue.DeQue<half>();
        AscendC::LocalTensor<half> yLocal = yQueue.DeQue<half>();
        AscendC::LocalTensor<half> zLocal = zQueue.AllocTensor<half>();
        AscendC::Add(zLocal, xLocal, yLocal, TILE_LENGTH);
        zQueue.EnQue(zLocal);
        xQueue.FreeTensor(xLocal);
        yQueue.FreeTensor(yLocal);
    }

    __aicore__ inline void CopyOut(int32_t tile)
    {
        AscendC::LocalTensor<half> zLocal = zQueue.DeQue<half>();
        AscendC::DataCopy(zGlobal[tile * TILE_LENGTH], zLocal, TILE_LENGTH);
        zQueue.FreeTensor(zLocal);
    }

    AscendC::TPipe pipe;
    AscendC::TQue<AscendC::QuePosition::VECIN, BUFFER_NUM> xQueue;
    AscendC::TQue<AscendC::QuePosition::VECIN, BUFFER_NUM> yQueue;
    AscendC::TQue<AscendC::QuePosition::VECOUT, BUFFER_NUM> zQueue;
    AscendC::GlobalTensor<half> xGlobal;
    AscendC::GlobalTensor<half> yGlobal;
    AscendC::GlobalTensor<half> zGlobal;
};

extern "C" __global__ __aicore__ void vector_add(GM_ADDR x, GM_ADDR y, GM_ADDR z)
{
    KernelAdd op;
    op.Init(x, y, z);
    op.Process();
}
