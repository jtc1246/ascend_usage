#include "acl_check.h"

#include <acl/acl_rt.h>
#include <aclnnop/aclnn_matmul.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char **argv)
{
    if (argc > 2) {
        std::fprintf(stderr, "Usage: %s [DEVICE_ID]\n", argv[0]);
        return 1;
    }
    const int device = argc == 2 ? std::atoi(argv[1]) : 0;
    constexpr int size = 32;
    constexpr std::size_t elements = size * size;
    constexpr std::size_t bytes = elements * sizeof(aclFloat16);

    std::vector<aclFloat16> a16(elements);
    std::vector<aclFloat16> b16(elements, aclFloatToFloat16(0.0f));
    std::vector<aclFloat16> c16(elements);
    std::vector<float> expected(elements);
    for (int row = 0; row < size; ++row) {
        for (int column = 0; column < size; ++column) {
            const std::size_t index = row * size + column;
            const float value = static_cast<float>(row) + static_cast<float>(column) * 0.5f;
            a16[index] = aclFloatToFloat16(value);
            expected[index] = value;
        }
        b16[row * size + row] = aclFloatToFloat16(1.0f);
    }

    CheckAcl(aclInit(nullptr), "aclInit");
    CheckAcl(aclrtSetDevice(device), "aclrtSetDevice");
    aclrtStream stream = nullptr;
    CheckAcl(aclrtCreateStream(&stream), "aclrtCreateStream");

    void *matrixA = nullptr;
    void *matrixB = nullptr;
    void *matrixC = nullptr;
    CheckAcl(aclrtMalloc(&matrixA, bytes, ACL_MEM_MALLOC_HUGE_FIRST), "allocate A");
    CheckAcl(aclrtMalloc(&matrixB, bytes, ACL_MEM_MALLOC_HUGE_FIRST), "allocate B");
    CheckAcl(aclrtMalloc(&matrixC, bytes, ACL_MEM_MALLOC_HUGE_FIRST), "allocate C");
    CheckAcl(aclrtMemcpy(matrixA, bytes, a16.data(), bytes, ACL_MEMCPY_HOST_TO_DEVICE), "copy A");
    CheckAcl(aclrtMemcpy(matrixB, bytes, b16.data(), bytes, ACL_MEMCPY_HOST_TO_DEVICE), "copy B");
    CheckAcl(aclrtMemset(matrixC, bytes, 0, bytes), "clear C");

    const std::array<int64_t, 2> shape{size, size};
    const std::array<int64_t, 2> strides{size, 1};
    aclTensor *tensorA = aclCreateTensor(shape.data(), shape.size(), ACL_FLOAT16, strides.data(),
                                         0, ACL_FORMAT_ND, shape.data(), shape.size(), matrixA);
    aclTensor *tensorB = aclCreateTensor(shape.data(), shape.size(), ACL_FLOAT16, strides.data(),
                                         0, ACL_FORMAT_ND, shape.data(), shape.size(), matrixB);
    aclTensor *tensorC = aclCreateTensor(shape.data(), shape.size(), ACL_FLOAT16, strides.data(),
                                         0, ACL_FORMAT_ND, shape.data(), shape.size(), matrixC);
    if (tensorA == nullptr || tensorB == nullptr || tensorC == nullptr) {
        std::fprintf(stderr, "ERROR: aclCreateTensor failed\n");
        return 2;
    }

    std::uint64_t workspaceSize = 0;
    aclOpExecutor *executor = nullptr;
    constexpr int8_t cubeMathType = 0;
    CheckAcl(aclnnMatmulGetWorkspaceSize(tensorA, tensorB, tensorC, cubeMathType,
                                         &workspaceSize, &executor),
             "aclnnMatmulGetWorkspaceSize");
    void *workspace = nullptr;
    if (workspaceSize > 0) {
        CheckAcl(aclrtMalloc(&workspace, workspaceSize, ACL_MEM_MALLOC_NORMAL_ONLY),
                 "allocate MatMul workspace");
    }
    CheckAcl(aclnnMatmul(workspace, workspaceSize, executor, stream), "aclnnMatmul");
    CheckAcl(aclrtSynchronizeStream(stream), "synchronize MatMul");
    CheckAcl(aclrtMemcpy(c16.data(), bytes, matrixC, bytes, ACL_MEMCPY_DEVICE_TO_HOST),
             "copy result");

    bool correct = true;
    for (std::size_t i = 0; i < elements; ++i) {
        const float actual = aclFloat16ToFloat(c16[i]);
        if (actual != expected[i]) {
            correct = false;
            std::fprintf(stderr, "C[%zu]=%.1f expected=%.1f\n", i, actual, expected[i]);
            break;
        }
    }
    std::printf("ACLNN FP16 MatMul demo: device=%d shape=%dx%dx%d workspace=%zu C[0]=%.1f C[last]=%.1f result=%s\n",
                device, size, size, size, workspaceSize, aclFloat16ToFloat(c16.front()),
                aclFloat16ToFloat(c16.back()), correct ? "PASS" : "FAIL");

    CheckAcl(aclDestroyTensor(tensorA), "destroy tensor A");
    CheckAcl(aclDestroyTensor(tensorB), "destroy tensor B");
    CheckAcl(aclDestroyTensor(tensorC), "destroy tensor C");
    if (workspace != nullptr) {
        CheckAcl(aclrtFree(workspace), "free workspace");
    }
    CheckAcl(aclrtFree(matrixA), "free A");
    CheckAcl(aclrtFree(matrixB), "free B");
    CheckAcl(aclrtFree(matrixC), "free C");
    CheckAcl(aclrtDestroyStream(stream), "aclrtDestroyStream");
    CheckAcl(aclrtResetDevice(device), "aclrtResetDevice");
    CheckAcl(aclFinalize(), "aclFinalize");
    return correct ? 0 : 3;
}
