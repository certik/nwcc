int
main() {
        static long long x = 9223372036854775807LL;
        static unsigned long long y = (9223372036854775807LL * 2ULL + 1ULL);
	float z = 4.0f;
	double a = 8.0;
	long double b = 50.3L;
        printf("%lld %llu\n", x, y);
	printf("%f %f %Lf\n", z, a, b);
}

