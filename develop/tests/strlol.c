void
lolcpy(char *dest, const char *src) {
	while ((*dest++ = *src++) != 0)
		;
}

char *
lolchr(const char *s, int ch) {
	do {
		if (*s == ch) {
			return (char *)s;
		}
	} while (*s++);
#define NULL ((void *)0)	
	return NULL;
}	

char *
lolcat(char *dest, const char *src) {
	return strcpy(strchr(dest, 0), src), (char *)src;
}

typedef unsigned long	size_t;

size_t
lollen(const char *s) {
	const char	*iter = s;

	while (*iter) ++iter;
	return iter - s;
}

int
main(void) {
	int	i;
	char	buf[123];
	struct {
		char	*str;
		size_t	len;
	} strings[] = {
		{ "hello", 0 },
		{ "blalalalallalaallalaalalla", 0 },
		{ "", 0 },
		{ " hmm         hmmmm                            hmmmm", 0 },
		{ NULL, 0 }
	};	

	for (i = 0; strings[i].str != NULL; ++i) {
		lolcpy(buf, strings[i].str);
		strings[i].len = lollen(buf);
		printf("`%s'=%lu(%lu)\n",
			buf, strings[i].len, lolchr(buf, 0) - buf);
	}	
	return 0;
}

