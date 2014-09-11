struct trash {
	int	x;
};	

struct trash gf = { 13 };
struct trash gf2 = { 46 };

struct trash2 {
	struct trash	t;
};

struct trash3 {
	int		before;
	struct trash	t;
	int		after;
};

int
main() {
	int	x = 1;
	struct trash lf = { 123 };
	struct trash lf2 = { 456 };
	struct trash lol = x? lf: lf2;
	struct trash lol2 = !x? gf: gf2; 
	struct trash2	junktrash = { lf };
	struct trash3	three0 = { 111, lf2 };
	struct trash3	three1 = { 444, lf, 555 };
	struct trash3	three2 = { .t = lf, .before = 555 };
	struct sanity {
		struct {
			int	x, y, z;
		} s;
	} s = {
		1, 2, 3
	};	


	printf("%d\n", lf.x);
	printf("%d\n", lol.x);
	printf("%d\n", lf2.x);
	printf("%d %d\n", lol.x, lol2.x);
	printf("%d\n", junktrash.t.x);

	printf("%d %d %d\n", three0.before, three0.t.x, three0.after);
	printf("%d %d %d\n", three1.before, three1.t.x, three1.after);
	printf("%d %d %d\n", three2.before, three2.t.x, three2.after);

	printf("%d %d %d\n", s.s.x, s.s.y, s.s.z);
}
