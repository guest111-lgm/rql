#include <stdio.h>

extern long lfird(void *, void *, void *, long);

int main(void)
{
    char buffer[32] = {0};
    long result = lfird((void *)0x1111, (void *)0x2222, buffer, 31);
    printf("result=%ld buffer=%s", result, buffer);
    return 0;
}
