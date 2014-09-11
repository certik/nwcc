#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>

static void
usage(void) {
	(void) fprintf(stderr, "check80 [-v] [-n number] [files ...]\n\n");
	(void) fprintf(stderr, "check80 checks whether lines in a file are too"
			"long");
	(void) fprintf(stderr, "By default ``too long'' means 80");
	(void) fprintf(stderr, "The -v option sets this limit to 72\n");
	(void) fprintf(stderr,
		       "The -n option sets this limit to a custom value\n");
	exit(EXIT_FAILURE);
}	

int
main(int argc, char **argv) {
	int	i;
	FILE	*fd;
	int	ch;
	int	warn;
	int	nchars;
	int	charsok = 80;
	int	lineno;


	while ((ch = getopt(argc, argv, "vn:")) != -1) {
		switch (ch) {
		case 'v':
			/*
			 * Check for vi with ``number'' option set -
			 * 72 chars ok
			 */
			charsok = 72;
			break;
		case 'n': {
			long	nch;
			char	*p;

			nch = strtol(optarg, &p, 10);
			if (*p || p == optarg
				|| nch == LONG_MIN || nch == LONG_MAX) {
				(void) fprintf(stderr, "Bad argument to -n\n");
				usage();
			}	
			
			charsok = nch; 
			break;
		}	
		default:
			usage();
		}
	}	
				

	argc -= optind;
	argv += optind;

	for (i = 0; i < argc; ++i) {
		if ((fd = fopen(argv[i], "r")) == NULL) {
			perror(argv[i]);
			continue;
		}

		lineno = 1;
		nchars = warn = 0;

		while ((ch = fgetc(fd)) != EOF) {
			if (ch == '\n') {
				if (warn) {
					printf(
					"%s: Line %d too long (%d chars)\n",
					argv[i], lineno, nchars);
				}	
				nchars = warn = 0;
				++lineno;
			} else {
				if (ch == '\t') nchars += 8;
				else ++nchars;
				if (nchars > charsok) {
					warn = 1;
				}	
			}
		}
		(void) fclose(fd);
	}
	return 0;
}	

