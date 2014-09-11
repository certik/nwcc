#include <stdio.h>
#include <string.h>

int
main() {
        char    buf[10] = { 0 };
        __builtin_memset(buf, 'x', 9);
        puts(buf);
        printf("%d\n", __builtin_strcmp(buf, "xxxxxxxxx"));
        printf("%d\n", __builtin_strcmp(buf, "xxxxxxyxx"));
        puts(__builtin_strcpy(buf, "hello"));
        __builtin_memcpy(buf, "xx", 2);
        puts(buf);
        printf("%d\n", __builtin_strlen(buf));
}
