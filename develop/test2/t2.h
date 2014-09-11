struct smallfp {
	float	f;
};

struct smallint {
	int	i;
};

struct smallmixed {
	int	i;
	float	f;
	int	i2;
};

void			getsmallfp(struct smallfp sf);
void			getsmallint(struct smallint si);
void			getsmallmixed(struct smallmixed sm);
struct smallfp		retsmallfp(float val);
struct smallint		retsmallint(int val);
struct smallmixed	retsmallmixed(int val0, float val1, int val2);
	
