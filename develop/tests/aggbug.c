struct trash {
	int	x;
};	

struct trash gf = { 13 };
struct trash gf2 = { 46 };


struct trash2 {
	struct trash	t;
};

int
main() {
	int	x = 1;
	struct trash lf = { 123 };
	struct trash lf2 = { 456 };
	struct trash lol = x? lf: lf2;
	struct trash lol2 = x? gf: gf2; 
	struct trash2	junktrash = { lf };
}
