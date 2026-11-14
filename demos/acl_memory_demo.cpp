#include "acl_check.h"

#include <acl/acl_rt.h>

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
    constexpr std::size_t count = 1024;
    constexpr std::size_t bytes = count * sizeof(std::uint32_t);

    std::vector<std::uint32_t> input(count);
    std::vector<std::uint32_t> output(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        input[i] = static_cast<std::uint32_t>(i * 3 + 1);
    }

    CheckAcl(aclInit(nullptr), "aclInit");
    CheckAcl(aclrtSetDevice(device), "aclrtSetDevice");
    aclrtStream stream = nullptr;
    CheckAcl(aclrtCreateStream(&stream), "aclrtCreateStream");

    void *source = nullptr;
    void *destination = nullptr;
    CheckAcl(aclrtMalloc(&source, bytes, ACL_MEM_MALLOC_HUGE_FIRST), "aclrtMalloc(source)");
    CheckAcl(aclrtMalloc(&destination, bytes, ACL_MEM_MALLOC_HUGE_FIRST),
             "aclrtMalloc(destination)");
    CheckAcl(aclrtMemcpyAsync(source, bytes, input.data(), bytes,
                              ACL_MEMCPY_HOST_TO_DEVICE, stream),
             "async host-to-device copy");
    CheckAcl(aclrtMemcpyAsync(destination, bytes, source, bytes,
                              ACL_MEMCPY_DEVICE_TO_DEVICE, stream),
             "async device-to-device copy");

    aclrtEvent complete = nullptr;
    CheckAcl(aclrtCreateEvent(&complete), "aclrtCreateEvent");
    CheckAcl(aclrtRecordEvent(complete, stream), "aclrtRecordEvent");
    CheckAcl(aclrtSynchronizeEvent(complete), "aclrtSynchronizeEvent");
    CheckAcl(aclrtMemcpy(output.data(), bytes, destination, bytes,
                         ACL_MEMCPY_DEVICE_TO_HOST),
             "device-to-host copy");

    bool correct = output == input;
    std::printf("ACL memory demo: device=%d elements=%zu first=%u last=%u result=%s\n",
                device, count, output.front(), output.back(), correct ? "PASS" : "FAIL");

    CheckAcl(aclrtDestroyEvent(complete), "aclrtDestroyEvent");
    CheckAcl(aclrtFree(source), "aclrtFree(source)");
    CheckAcl(aclrtFree(destination), "aclrtFree(destination)");
    CheckAcl(aclrtDestroyStream(stream), "aclrtDestroyStream");
    CheckAcl(aclrtResetDevice(device), "aclrtResetDevice");
    CheckAcl(aclFinalize(), "aclFinalize");
    return correct ? 0 : 3;
}
