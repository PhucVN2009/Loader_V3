#pragma once

#include <sys/un.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <inttypes.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <Dobby/dobby.h>

typedef unsigned long ulong;

#if defined(__arm__)
#include <SubstrateHook.h>
#elif defined(__aarch64__)
#include <And64InlineHook.hpp>
#endif

#define LOG_TAG "mk"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

void hook(void *address, void *new_fn, void **old_fn);
#define HOOK(address, new_fn, old_fn) hook(reinterpret_cast<void*>((address)), reinterpret_cast<void*>(new_fn), reinterpret_cast<void**>(old_fn));

inline pid_t pid = 0;

inline void hook(void *address, void *new_fn, void **old_fn) {
#if defined(__arm__)
    MSHookFunction(address, new_fn, old_fn);
#elif defined(__aarch64__)
    A64HookFunction(address, new_fn, old_fn);
#endif
}

inline uintptr_t get_module_base(int pid, const char *module_name)
{
	FILE *fp;
	uintptr_t addr = 0;
	char *pch;
	char filename[32];
	char line[1024];
	snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);
	fp = fopen(filename, "r");
	if (fp != NULL)
	{
		while (fgets(line, sizeof(line), fp))
		{
			if (strstr(line, module_name))
			{
				pch = strtok(line, "-");
				addr = (uintptr_t)strtoul(pch, NULL, 16);
				break;
			}
		}
		fclose(fp);
	}
	return addr;
}
