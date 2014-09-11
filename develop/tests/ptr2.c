int
main(void) {
	char	buf[2][128];
	char	(*p)[128];

	p = buf;
	strcpy((++p)[0], "hello");
	puts(&buf[1][0]);
}	

