int
main(void) {
	char	buf[2][128];
	char	(*p)[128];

	p = buf;
	(*p)[0] = 'h';
	(*p)[1] = '\0';
	puts(buf[0]);
	++p;
	strcpy(p[0], "tee-hee");
	puts(buf[1]);
}

