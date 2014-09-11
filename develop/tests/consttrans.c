//#include "scope.h"

struct dec_block {
	void	**data;
	int	nslots;
};

int
main() {
	struct dec_block	*db;
	struct dec_block	d;
	int			res;
	int			i = 123;
	unsigned		ui = 456;
	long			l = 512L;
	unsigned long		lu = 767LU;
	long long		ll = 0x1ffffffffLL;
	unsigned long long	llu = 0x1ffffffffLLU;

	db = &d;
	d.nslots = 10;

	res = sizeof *db->data * db->nslots;
	printf("%d\n", res);
	res = db->nslots * sizeof *db->data;
	printf("%d\n", res);
	res = sizeof *db->data * db->nslots;
	printf("%d\n", res);

	/*
	 * Multiplication with small constant powers of 2 (and not powers
	 * of 2 for comparison)
	 */
	printf("%d %d %d %d %d %d %d %d\n",
		i*3,  3*i,  i*0,  0*i,   i*4,   4*i,   16*i,   i*16);
	printf("%d %d %d %d %d %d %d %d\n",
		i*-3,  -3*i,  i*0,  0*i,   i*-4,   -4*i,   -16*i,   i*-16);
	printf("%d %d %d %d %d %d %d %d\n",
		-i*-3,  -3*-i,  -i*0,  0*-i,   -i*-4,   -4*-i,   -16*-i,   -i*-16);
	printf("%d %d %d %d %d %d %d %d\n",
		-i*3,  3*-i,  -i*0,  0*-i,   -i*4,   4*-i,   16*-i,   -i*16);

	printf("%u %u %u %u %u %u %u %u\n",
		ui*3,  3*ui,  ui*0,  0*ui,   ui*4,   4*ui,   16*ui,   ui*16);
	printf("%u %u %u %u %u %u %u %u\n",
		ui*-3,  -3*ui,  ui*0,  0*ui,   ui*-4,   -4*ui,   -16*ui,   ui*-16);
	printf("%u %u %u %u %u %u %u %u\n",
		-ui*-3,  -3*-ui,  -ui*0,  0*-ui,   -ui*-4,   -4*-ui,   -16*-ui,   -ui*-16);
	printf("%u %u %u %u %u %u %u %u\n",
		-ui*3,  3*-ui,  -ui*0,  0*-ui,   -ui*4,   4*-ui,   16*-ui,   -ui*16);

	printf("%ld %ld %ld %ld %ld %ld %ld %ld\n",
		l*3,  3*l,  l*0,  0*l,   l*4,   4*l,   16*l,   l*16);
	printf("%ld %ld %ld %ld %ld %ld %ld %ld\n",
		l*-3,  -3*l,  l*0,  0*l,   l*-4,   -4*l,   -16*l,   l*-16);
	printf("%ld %ld %ld %ld %ld %ld %ld %ld\n",
		-l*-3,  -3*-l,  -l*0,  0*-l,   -l*-4,   -4*-l,   -16*-l,   -l*-16);
	printf("%ld %ld %ld %ld %ld %ld %ld %ld\n",
		-l*3,  3*-l,  -l*0,  0*-l,   -l*4,   4*-l,   16*-l,   -l*16);

	printf("%lu %lu %lu %lu %lu %lu %lu %lu\n",
		lu*3,  3*lu,  lu*0,  0*lu,   lu*4,   4*lu,   16*lu,   lu*16);
	printf("%lu %lu %lu %lu %lu %lu %lu %lu\n",
		lu*-3,  -3*lu,  lu*0,  0*lu,   lu*-4,   -4*lu,   -16*lu,   lu*-16);
	printf("%lu %lu %lu %lu %lu %lu %lu %lu\n",
		-lu*-3,  -3*-lu,  -lu*0,  0*-lu,   -lu*-4,   -4*-lu,   -16*-lu,   -lu*-16);
	printf("%lu %lu %lu %lu %lu %lu %lu %lu\n",
		-lu*3,  3*-lu,  -lu*0,  0*-lu,   -lu*4,   4*-lu,   16*-lu,   -lu*16);

	printf("%lld %lld %lld %lld %lld %lld %lld %lld\n",
		ll*3,  3*ll,  ll*0,  0*ll,   ll*4,   4*ll,   16*ll,   ll*16);
	printf("%lld %lld %lld %lld %lld %lld %lld %lld\n",
		ll*-3,  -3*ll,  ll*0,  0*ll,   ll*-4,   -4*ll,   -16*ll,   ll*-16);
	printf("%lld %lld %lld %lld %lld %lld %lld %lld\n",
		-ll*-3,  -3*-ll,  -ll*0,  0*-ll,   -ll*-4,   -4*-ll,   -16*-ll,   -ll*-16);
	printf("%lld %lld %lld %lld %lld %lld %lld %lld\n",
		-ll*3,  3*-ll,  -ll*0,  0*-ll,   -ll*4,   4*-ll,   16*-ll,   -ll*16);

	printf("%llu %llu %llu %llu %llu %llu %llu %llu\n",
		llu*3,  3*llu,  llu*0,  0*llu,   llu*4,   4*llu,   16*llu,   llu*16);
	printf("%llu %llu %llu %llu %llu %llu %llu %llu\n",
		llu*-3,  -3*llu,  llu*0,  0*llu,   llu*-4,   -4*llu,   -16*llu,   llu*-16);
	printf("%llu %llu %llu %llu %llu %llu %llu %llu\n",
		-llu*-3,  -3*-llu,  -llu*0,  0*-llu,   -llu*-4,   -4*-llu,   -16*-llu,   -llu*-16);
	printf("%llu %llu %llu %llu %llu %llu %llu %llu\n",
		-llu*3,  3*-llu,  -llu*0,  0*-llu,   -llu*4,   4*-llu,   16*-llu,   -llu*16);

	/*
	 * Division with small constant powers of 2 (and not powers
	 * of 2 for comparison)
	 */
	printf("%d %d %d %d %d %d %d\n",
		i/3,  3/i, 0/i,   i/4,   4/i,   16/i,   i/16);
	printf("%d %d %d %d %d %d %d\n",
		i/-3,  -3/i, 0/i,   i/-4,   -4/i,   -16/i,   i/-16);
	printf("%d %d %d %d %d %d %d\n",
		-i/-3,  -3/-i, 0/-i,   -i/-4,   -4/-i,   -16/-i,   -i/-16);
	printf("%d %d %d %d %d %d %d\n",
		-i/3,  3/-i,  0/-i,   -i/4,   4/-i,   16/-i,   -i/16);

	printf("%u %u %u %u %u %u %u\n",
		ui/3,  3/ui, 0/ui,   ui/4,   4/ui,   16/ui,   ui/16);
	printf("%u %u %u %u %u %u %u\n",
		ui/-3,  -3/ui, 0/ui,   ui/-4,   -4/ui,   -16/ui,   ui/-16);
	printf("%u %u %u %u %u %u %u\n",
		-ui/-3,  -3/-ui,0/-ui,   -ui/-4,   -4/-ui,   -16/-ui,   -ui/-16);
	printf("%u %u %u %u %u %u %u\n",
		-ui/3,  3/-ui, 0/-ui,   -ui/4,   4/-ui,   16/-ui,   -ui/16);

	printf("%ld %ld %ld %ld %ld %ld %ld\n",
		l/3,  3/l, 0/l,   l/4,   4/l,   16/l,   l/16);
	printf("%ld %ld %ld %ld %ld %ld %ld\n",
		l/-3,  -3/l, 0/l,   l/-4,   -4/l,   -16/l,   l/-16);
	printf("%ld %ld %ld %ld %ld %ld %ld\n",
		-l/-3,  -3/-l, 0/-l,   -l/-4,   -4/-l,   -16/-l,   -l/-16);
	printf("%ld %ld %ld %ld %ld %ld %ld\n",
		-l/3,  3/-l, 0/-l,   -l/4,   4/-l,   16/-l,   -l/16);

	printf("%lu %lu %lu %lu %lu %lu %lu\n",
		lu/3,  3/lu, 0/lu,   lu/4,   4/lu,   16/lu,   lu/16);
	printf("%lu %lu %lu %lu %lu %lu %lu\n",
		lu/-3,  -3/lu, 0/lu,   lu/-4,   -4/lu,   -16/lu,   lu/-16);
	printf("%lu %lu %lu %lu %lu %lu %lu\n",
		-lu/-3,  -3/-lu, 0/-lu,   -lu/-4,   -4/-lu,   -16/-lu,   -lu/-16);
	printf("%lu %lu %lu %lu %lu %lu %lu\n",
		-lu/3,  3/-lu, 0/-lu,   -lu/4,   4/-lu,   16/-lu,   -lu/16);

	printf("%lld %lld %lld %lld %lld %lld %lld\n",
		ll/3,  3/ll, 0/ll,   ll/4,   4/ll,   16/ll,   ll/16);
	printf("%lld %lld %lld %lld %lld %lld %lld\n",
		ll/-3,  -3/ll, 0/ll,   ll/-4,   -4/ll,   -16/ll,   ll/-16);
	printf("%lld %lld %lld %lld %lld %lld %lld\n",
		-ll/-3,  -3/-ll, 0/-ll,   -ll/-4,   -4/-ll,   -16/-ll,   -ll/-16);
	printf("%lld %lld %lld %lld %lld %lld %lld\n",
		-ll/3,  3/-ll, 0/-ll,   -ll/4,   4/-ll,   16/-ll,   -ll/16);

	printf("%llu %llu %llu %llu %llu %llu %llu\n",
		llu/3,  3/llu,  0/llu,   llu/4,   4/llu,   16/llu,   llu/16);
	printf("%llu %llu %llu %llu %llu %llu %llu\n",
		llu/-3,  -3/llu, 0/llu,   llu/-4,   -4/llu,   -16/llu,   llu/-16);
	printf("%llu %llu %llu %llu %llu %llu %llu\n",
		-llu/-3,  -3/-llu, 0/-llu,   -llu/-4,   -4/-llu,   -16/-llu,   -llu/-16);
	printf("%llu %llu %llu %llu %llu %llu %llu\n",
		-llu/3,  3/-llu,  0/-llu,   -llu/4,   4/-llu,   16/-llu,   -llu/16);
}

