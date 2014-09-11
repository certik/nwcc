extern int	puts(const char *);

void
func() {
	int	(*ptr)(const char *);
	ptr = puts;
	ptr("hello");
}

