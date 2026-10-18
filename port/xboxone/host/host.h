#pragma once

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#ifndef EGL_EGLEXT_PROTOTYPES
#define EGL_EGLEXT_PROTOTYPES 1
#endif

#include <stddef.h>
#include <stdint.h>
#include "halo_android_abi.h"

#if defined(__clang__) && defined(_M_X64)
#define GUEST_ABI __attribute__((sysv_abi))
#elif defined(_MSC_VER)
#define GUEST_ABI
#else
#define GUEST_ABI
#endif

enum host_log_level { HOST_LOG_INFO, HOST_LOG_WARN, HOST_LOG_ERROR };

struct host_guest_image {
    const struct halo_guest_header *header;
    uint32_t base;
    uint32_t end;
};

extern struct host_guest_image host_image;

void host_logf(enum host_log_level level, const char *format, ...);
void GUEST_ABI host_log(const char *message);
void GUEST_ABI host_abort(const char *reason);
void GUEST_ABI host_exit(int code);
int GUEST_ABI host_errno(void);
int GUEST_ABI host_decompress_map(const char *source_path, const char *destination_path,
    uint32_t decompressed_size, uint32_t destination_size);
void host_fatal(const char *format, ...);

int host_load_image(const void *elf, size_t size);
int host_memory_initialize(uint32_t base, uint32_t size);
void *host_low_map(size_t size, int protection);
void host_low_unmap(void *pointer, size_t size);
int host_low_owns(uintptr_t address, size_t size);

long GUEST_ABI host_guest_mmap(uint64_t address, uint64_t size, int protection, int flags, int fd, int64_t offset);
long GUEST_ABI host_guest_munmap(uint64_t address, uint64_t size);
long GUEST_ABI host_guest_mprotect(uint64_t address, uint64_t size, int protection);
long GUEST_ABI host_guest_syscall(int64_t number, int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f);
long GUEST_ABI host_guest_clone(void *child_stack, unsigned long flags, void *parent_tid, void *tls, void *child_tid);
int GUEST_ABI host_guest_futex(int *uaddr, int futex_op, int val, const void *timeout, int *uaddr2, int val3);

int host_guest_call_direct(uintptr_t entry_point, uint32_t argc, const uint32_t *argv);
int host_guest_call_trampoline(uintptr_t entry_point, uint32_t argc, const uint32_t *argv);
uint64_t GUEST_ABI host_native_stack_get(void);

void *host_resolve_import(const char *name);
void *host_gl_resolve(const char *name);
void *host_gl_thunk(const char *name);
void host_sdl_set_backbuffer_size(int width, int height);
int host_egl_initialize(void);
void host_egl_swap_buffers(void);
void *host_egl_get_proc_address(const char *procname);

void GUEST_ABI hostgl_glDrawElementsBaseVertex(unsigned int mode, int count, unsigned int type, const void *indices, int basevertex);
void GUEST_ABI hostgl_glCopyImageSubData(
    unsigned int srcName, unsigned int srcTarget, int srcLevel, int srcX, int srcY, int srcZ,
    long long dstName, long long dstTarget, long long dstLevel, long long dstX, long long dstY, long long dstZ,
    long long srcWidth, long long srcHeight, long long srcDepth);
void GUEST_ABI hostgl_glGetIntegerv(unsigned int pname, int *params);
void GUEST_ABI hostgl_glTexParameteri(unsigned int target, unsigned int pname, int param);
void GUEST_ABI hostgl_glTexParameterf(unsigned int target, unsigned int pname, float param);
uint32_t GUEST_ABI hostgl_glGetString(unsigned int name);

void GUEST_ABI host_memory_watch_initialize(void);
void GUEST_ABI host_memory_watch_protect(uint32_t address, uint32_t size);
void GUEST_ABI host_memory_watch_begin_frame(void);
uint32_t GUEST_ABI host_memory_watch_generation(uint32_t address, uint32_t size);
uint32_t GUEST_ABI host_memory_watch_serial(void);
void GUEST_ABI host_memory_watch_prepare_write(uint32_t address, uint32_t size);
void GUEST_ABI host_memory_watch_forget(uint32_t address, uint32_t size);
