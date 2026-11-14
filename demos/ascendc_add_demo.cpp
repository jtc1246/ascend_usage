#include "acl_check.h"
#include "aclrtlaunch_vector_add.h"

#include <acl/acl_rt.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
constexpr std::size_t ELEMENTS = 8 * 2048;
constexpr std::uint32_t BLOCK_DIM = 8;
}

int main(int argc, char **argv)
{
    if (argc > 2) {
        std::fprintf(stderr, "Usage: %s [DEVICE_ID]\n", argv[0]);
        return 1;
    }
    const int device = argc == 2 ? std::atoi(argv[1]) : 0;
    constexpr std::size_t bytes = ELEMENTS * sizeof(aclFloat16);
    std::vector<aclFloat16> x(ELEMENTS);
    std::vector<aclFloat16> y(ELEMENTS);
    std::vector<aclFloat16> z(ELEMENTS);
    for (std::size_t i = 0; i < ELEMENTS; ++i) {
        x[i] = aclFloatToFloat16(static_cast<float>(i % 17) * 0.25f);
        y[i] = aclFloatToFloat16(static_cast<float>(i % 5));
    }

    CheckAcl(aclInit(nullptr), "aclInit");
    CheckAcl(aclrtSetDevice(device), "aclrtSetDevice");
    aclrtStream stream = nullptr;
    CheckAcl(aclrtCreateStream(&stream), "aclrtCreateStream");

    void *deviceX = nullptr;
    void *deviceY = nullptr;
    void *deviceZ = nullptr;
    CheckAcl(aclrtMalloc(&deviceX, bytes, ACL_MEM_MALLOC_HUGE_FIRST), "allocate X");
    CheckAcl(aclrtMalloc(&deviceY, bytes, ACL_MEM_MALLOC_HUGE_FIRST), "allocate Y");
    CheckAcl(aclrtMalloc(&deviceZ, bytes, ACL_MEM_MALLOC_HUGE_FIRST), "allocate Z");
    CheckAcl(aclrtMemcpy(deviceX, bytes, x.data(), bytes, ACL_MEMCPY_HOST_TO_DEVICE), "copy X");
    CheckAcl(aclrtMemcpy(deviceY, bytes, y.data(), bytes, ACL_MEMCPY_HOST_TO_DEVICE), "copy Y");

    CheckAcl(ACLRT_LAUNCH_KERNEL(vector_add)(BLOCK_DIM, stream, deviceX, deviceY, deviceZ),
             "launch vector_add");
    CheckAcl(aclrtSynchronizeStream(stream), "synchronize vector_add");
    CheckAcl(aclrtMemcpy(z.data(), bytes, deviceZ, bytes, ACL_MEMCPY_DEVICE_TO_HOST),
             "copy result");

    bool correct = true;
    std::size_t mismatch = 0;
    for (std::size_t i = 0; i < ELEMENTS; ++i) {
        const float expected = aclFloat16ToFloat(x[i]) + aclFloat16ToFloat(y[i]);
        if (aclFloat16ToFloat(z[i]) != expected) {
            correct = false;
            mismatch = i + 1;
            break;
        }
    }
    std::printf("Ascend C Add demo: device=%d elements=%zu z[0]=%.2f z[last]=%.2f result=%s\n",
                device, ELEMENTS, aclFloat16ToFloat(z.front()), aclFloat16ToFloat(z.back()),
                correct ? "PASS" : "FAIL");
    if (!correct) {
        std::fprintf(stderr, "First mismatch at index %zu\n", mismatch - 1);
    }

    CheckAcl(aclrtFree(deviceX), "free X");
    CheckAcl(aclrtFree(deviceY), "free Y");
    CheckAcl(aclrtFree(deviceZ), "free Z");
    CheckAcl(aclrtDestroyStream(stream), "aclrtDestroyStream");
    CheckAcl(aclrtResetDevice(device), "aclrtResetDevice");
    CheckAcl(aclFinalize(), "aclFinalize");
    return correct ? 0 : 3;
}
