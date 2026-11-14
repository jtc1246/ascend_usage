#pragma once

#include <acl/acl.h>

#include <cstdio>
#include <cstdlib>

inline void CheckAcl(aclError status, const char *operation)
{
    if (status != ACL_SUCCESS) {
        std::fprintf(stderr, "ERROR: %s failed, aclError=%d\n", operation,
                     static_cast<int>(status));
        std::exit(2);
    }
}
