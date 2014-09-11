void
f(signed long long int x)
{
	long long	bogus = -0x80000000LL;
	if (x < bogus)
		puts("bug");
}

int
main ()
{
  f (0);
  return 0;
}

