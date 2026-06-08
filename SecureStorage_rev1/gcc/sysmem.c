/* GCC heap: minimal _sbrk for nano.specs */
#include <errno.h>
#include <sys/types.h>

extern char _end;
extern char _estack;
extern char _Min_Stack_Size;

static char *heap_end = &_end;

void *_sbrk(ptrdiff_t incr)
{
    char *stack_limit = &_estack - (ptrdiff_t)&_Min_Stack_Size;
    char *prev = heap_end;

    if (heap_end + incr > stack_limit)
    {
        errno = ENOMEM;
        return (void *)-1;
    }
    heap_end += incr;
    return (void *)prev;
}
