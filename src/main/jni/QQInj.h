#pragma once

#include <cstdint>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

bool remote_inject(pid_t pid);

#ifdef __cplusplus
}
#endif
