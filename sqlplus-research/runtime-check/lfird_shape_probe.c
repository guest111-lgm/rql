#define _GNU_SOURCE

#include <dlfcn.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *(*lfiostd_function)(void *, unsigned short, void *);
typedef long (*lfird_function)(void *, void *, void *, long);

static sigjmp_buf jump_point;

static void stop_on_fault(int signal_number)
{
    siglongjmp(jump_point, signal_number);
}

static void print_object(void *object)
{
    size_t offset;
    unsigned char *bytes = (unsigned char *)object;
    unsigned char *stream = *(unsigned char **)(bytes + 0x28);

    printf("lfiostd object=%p\n", object);
    for (offset = 0; offset <= 0x38; offset += 8)
        printf("  +0x%02zx qword=0x%016llx\n", offset,
               (unsigned long long)*(uint64_t *)(bytes + offset));
    printf("  +0x10 word=0x%04x +0x30 word=0x%04x\n",
           *(unsigned short *)(bytes + 0x10),
           *(unsigned short *)(bytes + 0x30));
    printf("  +0x28 stream=%p stream[0]=%p stream[0xc]=0x%02x\n",
           (void *)stream, *(void **)stream, stream[0xc]);
}

static void *lookup_function(void *handle, const char *name, size_t size,
                              void *function_storage)
{
    void *symbol = dlsym(handle, name);
    memcpy(function_storage, &symbol, size);
    return symbol;
}

int main(int argc, char **argv)
{
    struct sigaction action;
    unsigned char context[0x1000];
    unsigned char context_tail[0x1000];
    unsigned char session_tail[0x1000];
    unsigned char buffer[64];
    void *handle;
    void *object;
    void *objects[3] = {0};
    void *chain;
    lfiostd_function lfiostd;
    lfird_function lfird;

    if (argc != 2) {
        fprintf(stderr, "usage: %s /path/to/libclntshcore.so.19.1\n", argv[0]);
        return 2;
    }
    memset(context, 0, sizeof(context));
    memset(context_tail, 0, sizeof(context_tail));
    memset(session_tail, 0, sizeof(session_tail));
    memset(buffer, 0, sizeof(buffer));
    *(void **)(context + 0x0) = context_tail;
    *(void **)(context + 0x8) = context_tail;
    *(void **)(context_tail + 0x18) = session_tail;
    chain = session_tail;
    *(void **)(session_tail + 0xd8) = chain;

    memset(&action, 0, sizeof(action));
    action.sa_handler = stop_on_fault;
    sigemptyset(&action.sa_mask);
    sigaction(SIGSEGV, &action, NULL);
    sigaction(SIGBUS, &action, NULL);
    sigaction(SIGABRT, &action, NULL);

    handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 1;
    }
    lookup_function(handle, "lfiostd", sizeof(lfiostd), &lfiostd);
    lookup_function(handle, "lfird", sizeof(lfird), &lfird);
    if (lfiostd == NULL || lfird == NULL) {
        fprintf(stderr, "required symbols not found: %s\n", dlerror());
        return 1;
    }
    if (sigsetjmp(jump_point, 1) != 0) {
        printf("lfird shape call faulted in the uninitialized fake context\n");
        dlclose(handle);
        return 0;
    }
    for (unsigned short type = 1; type <= 3; ++type) {
        object = lfiostd(context, type, NULL);
        objects[type - 1] = object;
        if (object == NULL) {
            printf("lfiostd type=%hu returned NULL\n", type);
        } else {
            printf("lfiostd type=%hu\n", type);
            print_object(object);
        }
    }
    if (objects[0] == NULL) {
        dlclose(handle);
        return 0;
    }
    printf("lfird result=%ld\n", lfird(context, objects[0], buffer, 63));
    printf("buffer-prefix=%s\n", buffer);
    dlclose(handle);
    return 0;
}
