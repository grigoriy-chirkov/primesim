#include <dlfcn.h>
#include <stdio.h>

void* malloc(size_t sz)
{
    void *libc_malloc = dlsym(RTLD_NEXT, "malloc");
    printf("malloc\n");
    return ((void *(*)(size_t))libc_malloc)(sz);
}

void free(void *p)
{
    void *libc_free= dlsym(RTLD_NEXT, "free");
    printf("free\n");
    return ((void (*)(void*))libc_free)(p);
}