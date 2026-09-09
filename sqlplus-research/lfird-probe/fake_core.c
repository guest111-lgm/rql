#include <string.h>

long lfird(void *arg1, void *arg2, void *buffer, long capacity)
{
    static const char response[] = "fake-core\n";
    (void)arg1;
    (void)arg2;
    if (buffer != NULL && capacity > 0) {
        size_t count = sizeof(response) - 1;
        if ((long)count > capacity)
            count = (size_t)capacity;
        memcpy(buffer, response, count);
        return (long)count;
    }
    return 0;
}
