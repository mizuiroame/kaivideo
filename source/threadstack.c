// Gives pthreads a roomier default stack. libounce's is 128 KB, and FFmpeg's
// video decoders run on such threads, some with big frames on deep call paths.
// Linked in with -Wl,--wrap=__syscall_thread_create (see the Makefile).
#include <stddef.h>

#define DEFAULT_STACK_SIZE (512 * 1024)

struct __pthread_t;

int __real___syscall_thread_create(struct __pthread_t **thread, void *(*func)(void *), void *arg, void *stack_addr,
                                   size_t stack_size);
int __wrap___syscall_thread_create(struct __pthread_t **thread, void *(*func)(void *), void *arg, void *stack_addr,
                                   size_t stack_size);

int __wrap___syscall_thread_create(struct __pthread_t **thread, void *(*func)(void *), void *arg, void *stack_addr,
                                   size_t stack_size)
{
    if (!stack_addr && !stack_size)
        stack_size = DEFAULT_STACK_SIZE;
    return __real___syscall_thread_create(thread, func, arg, stack_addr, stack_size);
}
