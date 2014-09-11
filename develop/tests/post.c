
static void
cpystr(char *dest, const char *src) {
	while (*src != 0) {
		*dest++ = *src++;
	}	
	*dest = 0;
}	


int
main(void) {
	int	i;
	char	buf[128];
	char	buf2[128];

	for (i = 0; i < 10; i++) {
		putchar('0' + i);
	}
	cpystr(buf, "hello world");
	printf("copied string `%s' \n", buf);
	return 0;
}	
