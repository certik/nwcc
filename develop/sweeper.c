/*
 * Written by Nils R. Weller on May 28 and June 2 2007
 *
 * nweller<at>tzi<dot>de
 */
#ifdef _AIX
#define _ALL_SOURCE 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <signal.h>


static struct termios	oldterm;
static int		term_changed;

static void
restore_term(void) {
	if (term_changed) {
		(void) tcsetattr(0, TCSANOW, &oldterm);
	}	
}

static void
sanity(void) {
	restore_term();
	system("reset");
}

static void
handle_sig(int s) {
	if (s == SIGWINCH) {
		static const char	text[] =
			"Sorry, cannot resize window\n";
		(void) s;
		(void) write(STDERR_FILENO, text, sizeof text - 1);
	}	
	sanity();
	_exit(EXIT_FAILURE);
}

static int
n_getch(void) {
        struct termios  t2;
        int             ch;
        static int      inited;

        if (!inited) {
                (void) tcgetattr(0, &oldterm);
                t2 = oldterm;
                t2.c_lflag &= ~(ICANON | ECHO);
                t2.c_cc[VMIN] = 1;
                t2.c_cc[VTIME] = 0;
		term_changed = 1;
                (void) tcsetattr(0, TCSANOW, &t2);
                atexit(restore_term);
                inited = 1;
        }
        ch = getchar();
        return ch;
}

#define FIELD_BASE_X	5
#define FIELD_BASE_Y	5

static void
get_rows_cols(int *rows, int *cols) {
        int     cmd = -1;

#if defined TIOCGWINSZ || defined TIOCGSIZE
        struct winsize  ws;
#    if defined TIOCGWINSZ
        cmd = TIOCGWINSZ;
#    else
        cmd = TIOCGSIZE;
#    endif
        if (ioctl(0, cmd, &ws) != -1) {
                *rows = ws.ws_row;
                *cols = ws.ws_col;
        } else {
                cmd = -1;
        }
#endif
        if (cmd == -1) {
                /* Assume 80x25 */
                *rows = 25;
                *cols = 80;
        }
}

struct field {
	int		width;
	int		height;
	int		n_bombs;
	int		fields_to_open;
	int		known_bombs;
	/*
	 * Bomb map. Lower 8 bits are used to flag the contents,
	 * upper 16 for the color
	 */
	unsigned	**bomb_map;
#define BMAP_FLAGS(val) ((val) & 0xff)
#define BMAP_COLOR(val) ((val) >> 16)
#define BMAP_SETCOL(field, color) \
	((field) |= (color) << 16) 

#define FIELD_BOMB	1
#define FIELD_MARKED	(1 << 1)
#define FIELD_OPEN	(1 << 2)
};		

static unsigned int
get_rand_val(void) {
	static int	fd;
	unsigned int	rc;

	if (fd == 0) {
		/* First run */
		if ((fd = open("/dev/urandom", O_RDONLY)) == -1) {
			fd = open("/dev/random", O_RDONLY);
		}
		srand((unsigned)time(NULL));
	}
	if (fd == -1) {
		struct timeval	tm;

		gettimeofday(&tm, NULL);
		rc = (unsigned)rand() + tm.tv_usec; 
	} else {
		if (read(fd, &rc, sizeof rc) == -1) {
			fd = -1;
			rc = get_rand_val();
		}
	}
	return rc;
}	


static void
put_bombs(struct field *f) {
	int		bombs;
	unsigned	**bomb_map = f->bomb_map;
	
	for (bombs = f->n_bombs; bombs > 0; --bombs) {
		int	rv_h = get_rand_val() % f->height;
		int	rv_w = get_rand_val() % f->width;

		if (bomb_map[rv_h][rv_w]) {
			/* Already taken, retry */
			++bombs;
			continue;
		}
		bomb_map[rv_h][rv_w] = FIELD_BOMB;
	}
}

static void *
n_xmalloc(size_t size) {
	void	*ret;

	if ((ret = malloc(size)) == NULL) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}
	return ret;
}

static void
setpos(int y, int x) {
	printf("\033[%d;%df", y, x);
}	

#define COL_B_GREEN	42
#define COL_B_YELLOW	43
#define COL_B_GREY	47
#define COL_B_RED	41
#define COL_F_GREEN	32
#define COL_F_MAGENTA	35
#define COL_F_RED	31
#define COL_F_BLUE	34

static int 
vprintcol(int fore, int back, const char *fmt, va_list va) {
	printf("\033[1;%d;%dm", fore, back);
	return vprintf(fmt, va);
}

static int 
printcol(int fore, int back, const char *fmt, ...) {
	va_list	va;
	int	rc;

	va_start(va, fmt);
	rc = vprintcol(fore, back, fmt, va);
	va_end(va);
	return rc;
}

static void
print_status(struct field *fieldp, const char *fmt, ...) {
	va_list	va;
	static int	max_written;
	int	rc;

	setpos(FIELD_BASE_Y+fieldp->height+2, FIELD_BASE_X);
	va_start(va, fmt);
	rc = vprintcol(COL_F_GREEN, COL_B_GREY, fmt, va);
	va_end(va);
	if (max_written == 0) {
		max_written = rc;
	} else if (rc < max_written) {
		/* Delete old output on same line */
		do {
			printcol(COL_F_GREEN, COL_B_GREY, " ");
		} while (++rc < max_written); 
	}
}

static void
draw_map(const struct field *fieldp) {
	int		i;
	char		*buf;
	char		*p;
	unsigned	**bombs = fieldp->bomb_map;

	buf = n_xmalloc(fieldp->width + 1);
	memset(buf, ' ', fieldp->width);
	buf[fieldp->width] = 0;
	
	/* The map starts at 5x5 */
	for (i = 0; i < fieldp->height; ++i) {
		int	bg;

		setpos(FIELD_BASE_Y+i, FIELD_BASE_X);
		if (i % 2) {
			bg = COL_B_GREY;
		} else {
			bg = COL_B_YELLOW;
		}
		for (p = buf; p < buf + fieldp->width; ++p) {
#if 0
			printf("\033[1;%d;%dm", 32, bg);
#endif
			BMAP_SETCOL(bombs[i][p - buf], bg);
			printcol(0, bg, bombs[i][p-buf] & FIELD_BOMB?
					/*"x"*/ " ": " ");
#if 0
			putchar(' ');
#endif
			if (bg == COL_B_GREY) {
				bg = COL_B_YELLOW;
			} else {
				bg = COL_B_GREY;
			}
		}
	}
}

static int
calc_neigh_count(int ypos, int xpos, struct field *fieldp) {
	int	neighbour_bombs = 0;


	/*
	 * Check upper field
	 *
	 *   x
	 *   c
	 */
	if (ypos > 0) {
		if (fieldp->bomb_map[ypos - 1][xpos] & FIELD_BOMB) {
			++neighbour_bombs;
		}	
	}
	/*
	 * Check lower field
	 */
	if (ypos < fieldp->height - 1) {
		if (fieldp->bomb_map[ypos + 1][xpos] & FIELD_BOMB) {
			++neighbour_bombs;
		}	
	}	
	if (xpos > 0) {
		/*
		 * Check left triple;
		 *
		 *  x
		 *  x c
		 *  x
		 */
		if (fieldp->bomb_map[ypos][xpos - 1] & FIELD_BOMB) {
			++neighbour_bombs;
		}	
		if (ypos > 0) {
			if (fieldp->bomb_map[ypos - 1][xpos - 1] & FIELD_BOMB) {
				++neighbour_bombs;
			}
		}	
		if (ypos < fieldp->height - 1) {
			if (fieldp->bomb_map[ypos + 1][xpos - 1] & FIELD_BOMB) {
				++neighbour_bombs;
			}
		}
	}
	if (xpos < fieldp->width - 1) { 
		/*
		 * Check right triple;
		 *
		 *     x
		 *  c  x
		 *     x
		 */
		if (fieldp->bomb_map[ypos][xpos + 1] & FIELD_BOMB) {
			++neighbour_bombs;
		}
		if (ypos > 0) {
			if (fieldp->bomb_map[ypos - 1][xpos + 1] & FIELD_BOMB) {
				++neighbour_bombs;
			}
		}	
		if (ypos < fieldp->height - 1) {
			if (fieldp->bomb_map[ypos + 1][xpos + 1] & FIELD_BOMB) {
				++neighbour_bombs;
			}
		}
	}
	return neighbour_bombs;
}

static int
open_field(struct field *fieldp, int cur_y_pos, int cur_x_pos) {
	int	neigh_count;

	if (fieldp->bomb_map[cur_y_pos][cur_x_pos] & FIELD_OPEN) {
		return -1;
	}
	if (fieldp->bomb_map[cur_y_pos][cur_x_pos] & FIELD_BOMB) {
		return -1;
	}
	neigh_count = calc_neigh_count(cur_y_pos, cur_x_pos, fieldp);
	setpos(FIELD_BASE_Y+cur_y_pos, FIELD_BASE_X+cur_x_pos);
	fieldp->bomb_map[cur_y_pos][cur_x_pos] |= FIELD_OPEN;
	--fieldp->fields_to_open;
	
	if (neigh_count == 0) {
		printcol(0, COL_B_GREEN, " ");

		/*
		 * Recursively open surrounding fields because
		 * there are no neighbouring bombs
		 */
		if (cur_x_pos > 0) {
			open_field(fieldp, cur_y_pos, cur_x_pos - 1);
			if (cur_y_pos > 0) {
				open_field(fieldp, cur_y_pos - 1, cur_x_pos - 1);
			}
			if (cur_y_pos < fieldp->height - 1) {
				open_field(fieldp, cur_y_pos + 1, cur_x_pos - 1);
			}
		}
		if (cur_x_pos < fieldp->width - 1) {
			open_field(fieldp, cur_y_pos, cur_x_pos + 1);
			if (cur_y_pos > 0) {
				open_field(fieldp, cur_y_pos - 1, cur_x_pos + 1);
			}
			if (cur_y_pos < fieldp->height - 1) {
				open_field(fieldp, cur_y_pos + 1, cur_x_pos + 1);
			}
		}
		if (cur_y_pos > 0) {
			open_field(fieldp, cur_y_pos - 1, cur_x_pos);
		}	
		if (cur_y_pos < fieldp->height - 1) {
			open_field(fieldp, cur_y_pos + 1, cur_x_pos);
		}	
	} else {
		static const int	neigh_cols[] = {
			0, /* unused */
			COL_F_BLUE,  /* 1 */
			COL_F_GREEN, /* 2 */
			COL_F_MAGENTA, /* 3 */
			COL_F_RED, /* 4 */
			COL_F_RED, /* 5 */
			COL_F_RED, /* 6 */
			COL_F_RED, /* 7 */
			COL_F_RED /* 8 */
		};
		printcol(neigh_cols[neigh_count], COL_B_GREEN,
			"%c", '0' + neigh_count);	
		return -1;
	}
	return 0;
}

static void
print_field_status(struct field *fieldp) {	
	print_status(fieldp,
		"%04d fields to open     %04d unknown bombs (of %04d)",
		fieldp->fields_to_open,
		fieldp->n_bombs - fieldp->known_bombs,
		fieldp->n_bombs);
}


static void
open_all_bombs(struct field *fieldp) {
	int	i;
	int	j;

	for (i = 0; i < fieldp->height; ++i) {
		for (j = 0; j < fieldp->width; ++j) {
			if (fieldp->bomb_map[i][j] & FIELD_BOMB) {
				setpos(FIELD_BASE_Y+i, FIELD_BASE_X+j);
				printcol(COL_F_RED, COL_B_RED, "*");
			}	
		}
	}
}
static int 
check_pos(int cur_x_pos, int cur_y_pos, struct field *fieldp) {
	int	flags;
	int	i;
	int	neigh_count;

	flags = BMAP_FLAGS(fieldp->bomb_map[cur_y_pos][cur_x_pos]);
	if (flags & FIELD_OPEN) {
		/* Already open - no effect */  
		return 0;
	} else if (flags & FIELD_MARKED) {
		/* Deny attempt to open marked field - first unmark it! */
		return 0;
	} else if (flags & FIELD_BOMB) {
		open_all_bombs(fieldp);
		return -1;
	}
	BMAP_SETCOL(fieldp->bomb_map[cur_y_pos][cur_x_pos], COL_B_GREEN);
	setpos(FIELD_BASE_Y+cur_y_pos, FIELD_BASE_X+cur_x_pos);
	
	/*
	 * Now check all directions and open fields
	 */
	open_field(fieldp, cur_y_pos, cur_x_pos);

	/*
	 * Print bomb/field info, then restore current cursor position
	 */
	print_field_status(fieldp);
	return 0;
}

static void
mark_pos(int cur_x_pos, int cur_y_pos, struct field *fieldp) {
	unsigned	**map = fieldp->bomb_map;
	int	backcol = BMAP_COLOR(map[cur_y_pos][cur_x_pos]);

	if (map[cur_y_pos][cur_x_pos] & FIELD_MARKED) {
		/* Is marked - unmark */
		--fieldp->known_bombs;
		printcol(COL_F_RED, backcol, " \b");
	} else {
		/* Not marked... but are there marks left? */
		if (fieldp->known_bombs == fieldp->n_bombs) {
			print_status(fieldp, "Error: You have already marked "
				"all bombs (%d)!", fieldp->n_bombs);
			setpos(cur_y_pos, cur_x_pos);
			return;
		} else {
			++fieldp->known_bombs;
			printcol(COL_F_RED, backcol, "/\b");
		}
	}
	map[cur_y_pos][cur_x_pos] ^= FIELD_MARKED;
	print_field_status(fieldp);
}


int
main() {
	int	term_width;
	int	term_height;
	int	cur_x_pos = 0;
	int	cur_y_pos = 0;
	int	i;
	unsigned int	starttime;
	unsigned	**bomb_map;
	static struct field /* {
		int		width;
		int		height;
		int		bombs;
		int		fields_to_open;
		int		known_bombs;
		unsigend	**bomb_map;
	} */ field_dimensions[] = {
		{ 10, 5, 10, 0, 0, NULL },
		{ 20, 10, 20, 0, 0, NULL },
		{ 30, 10, 40, 0, 0, NULL },
		{ 30, 15, 40, 0, 0, NULL },
		{ 20, 20, 80, 0, 0, NULL },
		{ 40, 20, 80, 0, 0, NULL },
		{ 60, 20, 80, 0, 0, NULL },
		{ -1, -1, -1, 0, 0, NULL },
		 
	};
	struct field	*fieldp = NULL;

	(void)atexit(sanity);
	(void)signal(SIGINT, handle_sig);
	
	get_rows_cols(&term_height, &term_width);
	setbuf(stdout, NULL);

	puts("Minesweeper clone, (c) Nils Weller 2007"); 
	puts("");
	puts("Controls:");
	puts("");
	puts("   Move left  - a or 4");
	puts("   Move right - d or 6");
	puts("   Move up    - w or 8");
	puts("   Move down  - s or 5");
	puts("   Open field - Space");
	puts("   Mark bomb  - Enter");
	puts("");
#if 0
	puts("The purpose of minesweeper is to find all bombs on");
	puts("the game map. You win the game by opening all fields");
	puts("that do not contain bombs.");
	puts("");
	puts("Open fields whose neighbouring fields contain one or");
	puts("more bombs are marked with the number of bombs around");
	puts("them. To" 
#endif
			

	for (;;) {
		int	ch;
		int	ch2;
	
		puts("Choose number of fields/bombs:");
		for (i = 0; field_dimensions[i].width != -1; ++i) {
			if ((field_dimensions[i].width + 10)
				> term_width) {
				break;
			}
			if ((field_dimensions[i].height + 10)
				> term_height) {
				break;
			}
				
			printf("    %d) %d x %d field   (%d bombs)\n",
				i+1,	
				field_dimensions[i].width,
				field_dimensions[i].height,
				field_dimensions[i].n_bombs);
		}
		printf("Enter the number of your choice: ");
		fflush(stdout);
		ch = getchar();
		do {
			ch2 = getchar();
		} while (ch2 != EOF && ch2 != '\n');
		if (isdigit(ch)) {
			ch -= '0' + 1;
			if (ch >= 0 && ch < i) {
				fieldp = &field_dimensions[ch];
				break;
			}
		}
				
		puts("Bad input, try again");
	}

	/* Clear screen */
	for (i = 0; i < term_height; ++i) {
		puts("");
	}

	bomb_map = n_xmalloc(fieldp->height * sizeof *bomb_map);
	for (i = 0; i < fieldp->height; ++i) {
		bomb_map[i] = n_xmalloc(fieldp->width * sizeof **bomb_map);
		memset(bomb_map[i], 0, fieldp->width * sizeof **bomb_map);
	}
	fieldp->bomb_map = bomb_map;
	fieldp->fields_to_open = fieldp->width * fieldp->height -
		fieldp->n_bombs;
	put_bombs(fieldp);
	draw_map(fieldp);

	starttime = time(NULL);
	/* Play the game! */
	for (;;) {
		int	ch;
		
		setpos(FIELD_BASE_Y+cur_y_pos, FIELD_BASE_X+cur_x_pos);
		ch = n_getch();
		if (ch == 'q') {
			raise(SIGINT);
		} else if (ch == 'w' || ch == '8') {
			/* Move up */
			if (cur_y_pos > 0) {
				--cur_y_pos;
			}
		} else if (ch == 'a' || ch == '4') {
			/* Move left */
			if (cur_x_pos > 0) {
				--cur_x_pos;
			}
		} else if (ch == 's' || ch == '5') {
			/* Move down */
			if (cur_y_pos < fieldp->height - 1) {
				++cur_y_pos;
			}
		} else if (ch == 'd' || ch == '6') {
			/* Move right */
			if (cur_x_pos < fieldp->width - 1) {
				++cur_x_pos;
			}	
		} else if (ch == ' ') {
			if (check_pos(cur_x_pos, cur_y_pos, fieldp) == -1) {
				print_status(fieldp,
					"You lose :-(");	
				break;
			}
			if (fieldp->fields_to_open <= 0) {
				if (fieldp->fields_to_open < 0) {
					puts("BUG...");
					abort();
				}
				open_all_bombs(fieldp);
				print_status(fieldp,
					"You win! Time:   %u seconds",
					(unsigned)time(NULL) - starttime); 
				break;
			}
		} else if (ch == '\n') {
			mark_pos(cur_x_pos, cur_y_pos, fieldp);
		}
	}
	(void) n_getch();
	return 0;
}

