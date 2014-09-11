typedef unsigned int u_int64_t __attribute__ ((__mode__ (__DI__)));

typedef struct {
 int f;
} Buffer;


void buffer_put_int64(Buffer *buffer, unsigned long long);
void buffer_put_int64(Buffer *buffer, u_int64_t value) {}

int
main() {
}

