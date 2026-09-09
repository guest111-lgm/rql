#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    void *handle;
    void *symbol;
    const char *error;

    if (argc != 2) {
        fprintf(stderr, "usage: %s /path/to/libclntshcore.so.19.1\n", argv[0]);
        return 2;
    }
    handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 1;
    }
    dlerror();
    symbol = dlsym(handle, "lfird");
    error = dlerror();
    if (error != NULL || symbol == NULL) {
        fprintf(stderr, "dlsym(lfird) failed: %s\n", error ? error : "null");
        dlclose(handle);
        return 1;
    }
    printf("loaded lfird=%p\n", symbol);
    dlclose(handle);
    return 0;
}
