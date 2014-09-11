#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

FILE *
exec_cmd(char *prog, char *format, ...) {
	va_list		v;
	char		buf[4096]	= { 0 };
	char		*p;
	int		err	= 0;
	int		len	= 0;
	FILE		*fd;

	va_start(v, format);

	strncpy(buf, prog, sizeof buf);
	buf[sizeof(buf) - 1] = 0;
	len = strlen(buf);
	
	while (*format) {
		printf("HAH %c\n", *format);
		switch (*format) {
		case '%':
			switch (*++format) {
			case 's':
				p = va_arg(v, char *);
				printf("doing ");
				fflush(stdout);
				printf("%s\n", p);

				if (strlen(p) > (sizeof buf - strlen(buf))) {
					(void) fprintf(stderr,
						"Arguments too long.\n");
					err = 1;
					break;
				}
				strcat(buf, p);
				len += strlen(p);
				break;
			default:
				(void) fprintf(stderr,
				"Unknown escape sequence - ignoring.\n");
				if (*format == 0) {
					err = 1;
				}
			}
			break;
		default:
			if (len < 4095) {
				buf[len] = *format;
				++len;
				buf[len] = 0;
			} else {
				fprintf(stderr, "Arguments too long.\n");
				err = 1;
				break;
			}
		}
		if (err == 1) {
			return NULL;
		}
		if (*++format == 0) {
			break;
		}
	}
	va_end(v);
	buf[len] = 0;
	fd = popen(buf, "r");
	return fd;
}

int
main(void) {
	FILE	*fd;
	char	buf[128];

	if (getcwd(buf, sizeof buf) == NULL) {
		perror("getcwd");
		return 1;
	}	

	if ((fd = exec_cmd("ls", " %s/n_%s", buf, "libc.c", NULL)) != NULL) {
		while (fgets(buf, sizeof buf, fd) != NULL) {
			char	*p = strrchr(buf, '/');
			printf("%s", p+1);
		}
		(void) pclose(fd);
	}
	return 0;
}

