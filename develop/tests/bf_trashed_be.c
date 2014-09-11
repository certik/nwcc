#include <stdio.h>

int
main() {
        struct bogus {
                unsigned int    gnu:1;
                unsigned int    blue:7;
        } b;
        unsigned char   *ucp;

        printf("%d\n", (int)sizeof b);
        memset(&b, 0, sizeof b);
        b.gnu = 1;
        for (ucp = (unsigned char *)&b; ucp != (unsigned char *)&b + sizeof b; ++ucp) {
                printf("%02x ", *ucp);
        }
        putchar('\n');
        b.blue = 0; /* Shall not affect previous output */
        for (ucp = (unsigned char *)&b; ucp != (unsigned char *)&b + sizeof b; ++ucp) {
                printf("%02x ", *ucp);
        }
        putchar('\n');
        b.blue = 0x7f;
        b.gnu = 0;
        for (ucp = (unsigned char *)&b; ucp != (unsigned char *)&b + sizeof b; ++ucp) {
                printf("%02x ", *ucp);
        }
        putchar('\n');

        return 0;
}
