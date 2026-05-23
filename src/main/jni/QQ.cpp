#include "QQInj.h"

#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <dlfcn.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <elf.h>
#include <link.h>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <android/log.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/uio.h>

// Tùy theo kiến trúc:
#if defined(__aarch64__)
#define __NR_process_vm_writev 270
#elif defined(__arm__)
#define __NR_process_vm_writev 377
#elif defined(__i386__)
#define __NR_process_vm_writev 348
#else
#error "Unsupported architecture"
#endif

ssize_t my_process_vm_writev(pid_t pid,
  const struct iovec* local_iov,
  unsigned long liovcnt,
  const struct iovec* remote_iov,
  unsigned long riovcnt,
  unsigned long flags) {
  return syscall(__NR_process_vm_writev, pid, local_iov, liovcnt, remote_iov, riovcnt, flags);
}
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "rohanpapa", __VA_ARGS__)

#if defined(__aarch64__)
#define REGS_STRUCT user_pt_regs
#define PTRACE_GETREGS PTRACE_GETREGSET
#define PTRACE_SETREGS PTRACE_SETREGSET
#else
#define REGS_STRUCT pt_regs
#endif

// Lấy đường dẫn của chính .so đang chạy
const char* get_self_so_path() {
  Dl_info info;
  if (dladdr((void*)get_self_so_path, &info)) {
    return info.dli_fname;
  }
  return nullptr;
}

// Viết dữ liệu vào bộ nhớ tiến trình khác
bool write_mem(pid_t pid, void* addr, const void* data, size_t len) {
  struct iovec local_iov = {
    .iov_base = const_cast<void*>(data),
    .iov_len = len
  };
  struct iovec remote_iov = {
    .iov_base = addr,
    .iov_len = len
  };
  ssize_t n = my_process_vm_writev(pid, &local_iov, 1, &remote_iov, 1, 0);
  return n == (ssize_t)len;
}

// Lấy địa chỉ base của thư viện từ /proc/<pid>/maps
uintptr_t get_module_base(pid_t pid, const char* libname) {
  char path[64],
  line[512];
  snprintf(path, sizeof(path), "/proc/%d/maps", pid);
  FILE* fp = fopen(path, "r");
  if (!fp) return 0;
  uintptr_t addr = 0;
  while (fgets(line, sizeof(line), fp)) {
    if (strstr(line, libname)) {
      sscanf(line, "%lx-%*lx", &addr);
      break;
    }
  }
  fclose(fp);
  return addr;
}

// Tìm offset của hàm trong thư viện hệ thống
uintptr_t get_remote_func_addr(const char* libname, const char* funcname) {
  void* handle = dlopen(libname, RTLD_NOW);
  void* func_addr = dlsym(handle, funcname);
  uintptr_t local_base = get_module_base(getpid(), libname);
  uintptr_t remote_offset = (uintptr_t)func_addr - local_base;
  dlclose(handle);
  return remote_offset;
}

// Inject chính .so hiện tại vào tiến trình có pid
bool remote_inject(pid_t pid) {
  const char* so_path = get_self_so_path();
  if (!so_path) {
    LOGD("Failed to get self .so path");
    return false;
  }

  LOGD("Injecting self: %s into PID %d", so_path, pid);

  if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) == -1) {
    LOGD("ptrace attach failed: %s", strerror(errno));
    return false;
  }

  waitpid(pid, NULL, 0);

  uintptr_t linker_base = get_module_base(pid, "linker64");
  if (!linker_base) linker_base = get_module_base(pid, "linker");

  uintptr_t dlopen_offset = get_remote_func_addr("linker64", "dlopen");
  if (!dlopen_offset) dlopen_offset = get_remote_func_addr("linker", "dlopen");

  uintptr_t remote_dlopen = linker_base + dlopen_offset;

  void* remote_str_addr = (void*)(linker_base + 0x10000);
  if (!write_mem(pid, remote_str_addr, so_path, strlen(so_path) + 1)) {
    LOGD("Failed to write .so path into remote process");
    ptrace(PTRACE_DETACH, pid, NULL, NULL);
    return false;
  }
  ptrace(PTRACE_DETACH, pid, NULL, NULL);
  LOGD("Written path only, you must implement remote call to dlopen");
  return true;
}