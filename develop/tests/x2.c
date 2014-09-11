int
main(void) {
	char	buf[128];
	int	i = 0;
	int	ch;

	while ((ch = getchar()) != -1) {
		if (i > 10) {
			puts("too many chars");
			exit(0);
		} else if (ch == '\n') {
			break;
		}
		buf[i] = ch;
		++i;
	}
	buf[i] = 0;
	printf("`%s'.", buf);
 }	 
