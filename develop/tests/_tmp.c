void
lolcpy(char *dest, const char *src) {
	while ((*dest++ = *src++) != 0)
		;
}

int
main(void) {
	int	i;
	char	buf[123];

	lolcpy(buf, "stuff");
	puts(buf);
	return 0;
}

