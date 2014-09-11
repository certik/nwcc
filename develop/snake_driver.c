#ifdef _AIX /* XXX */
#define _XOPEN_SOURCE 500
#endif


#include "snake_driver.h"
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <termios.h>

struct snake_atom {
	int			row;
	int			col;
	struct snake_atom	*prev;
	struct snake_atom	*next;
};	

static struct snake_atom	*snake_head;
static struct snake_atom	*snake_tail;

static int	rows;
static int	cols;
static int	food_row;
static int	food_col;
/*
static int	bonus_row;
static int	bonus_col;
*/

#define DIR_LEFT	1
#define DIR_RIGHT	2
#define DIR_UP		3
#define DIR_DOWN	4

static pthread_mutex_t	dirmut = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t	dircond = PTHREAD_COND_INITIALIZER;
static int		direction = DIR_RIGHT;
static int		needupdate = 0;
static int		atoms_left;

static unsigned long	points = 0;

static struct termios	oldterm;

static void
restore_term(void) {
	(void) tcsetattr(0, TCSANOW, &oldterm);
}	

static void 
sigwinch(int s) {
	static const char	text[] =
		"Sorry, cannot resize window\n";
	(void) s;
	(void) write(1, text, sizeof text - 1);
	restore_term();
	_exit(EXIT_FAILURE);
}


static void 
new_snake_atom(int row, int col) {
	struct snake_atom	*ret = malloc(sizeof *ret);

	ret->row = row;
	ret->col = col;
	ret->next = snake_head;
	if (snake_head != NULL) {
		snake_head->prev = ret;
	} else {
		snake_tail = ret;
	}	
	snake_head = ret;
}	

static int
position_taken(int row, int col) {
	struct snake_atom	*atom;

	for (atom = snake_head->next; atom != NULL; atom = atom->next) {
		if (atom->col == col
			&& atom->row == row) {
			return 1;
		}
	}	
	return 0;
}

static void
put_food(int justdraw) {
	if (!justdraw) {
		/* Calculate position for new item */
		food_row = rand() % (rows - 2);
		food_col = rand() % (cols - 2);

		while (food_row < 2) ++food_row;
		while (food_col < 2) ++food_col;

		while (position_taken(food_row, food_col)) {
			food_col = (food_col + 1) % (cols - 2);
			if (food_col == 0) {
				while (food_col < 2) ++food_col;
				food_row = (food_row + 1) % (rows - 2);
				while (food_row < 2) {
					++food_row;
				}	
			}	
		}
	}	

	printf("\033[%d;%df", food_row, food_col);
	printf("o");
}	

static int 
advance(void) {
	int			curcol = snake_head->col;
	int			currow = snake_head->row;
	int			curdir;
	int			done = 0;
	struct snake_atom	*atom;
	
	pthread_mutex_lock(&dirmut);
	curdir = direction;
	needupdate = 0;
	pthread_cond_signal(&dircond);
	pthread_mutex_unlock(&dirmut);

	switch (curdir) {
	case DIR_LEFT:
		if (--curcol < 0) {
			curcol = cols;
		}
		break;
	case DIR_RIGHT:
		if (++curcol >= cols) {
			curcol = 1;
		}	
		break;
	case DIR_UP:
		if (--currow < 0) {
			currow = rows;
		}	
		break;
	case DIR_DOWN:	
		if (++currow >= rows) {
			currow = 1;
		}
		break;
	default:
		abort();
	}


	if (currow == food_row && curcol == food_col) {
		points += 7;
		new_snake_atom(currow, curcol);
		atoms_left = 3;
		put_food(0);
		done = 1;
	} else {
		if (atoms_left > 0) {
			--atoms_left;
			new_snake_atom(currow, curcol);
			done = 1;
		} else {	
			/* All atoms take position of preceding atom */
			for (atom = snake_tail; atom != snake_head; atom = atom->prev) {
				atom->row = atom->prev->row;
				atom->col = atom->prev->col;
			}
			snake_head->row = currow;
			snake_head->col = curcol;
			put_food(1); /* XXX :( */
		}	
	}
	printf("\033[%d;%df", currow, curcol);
	printf("x\b");
	if (done) {
		return curdir;
	}	

	if (position_taken(currow, curcol)) {
		printf("\033[%d;%df", rows - 1, 1);
		printf(" \b");
		puts("Sorry, you lose");
		printf("Your score is %lu\n", points);
		exit(EXIT_FAILURE);
	}	

	/* Delete tail atom */
	printf("\033[%d;%df", snake_tail->row, snake_tail->col);
	printf(" \b");
	return curdir;
}


static void *
display_thread(void *arg) {
	int	delay = *(int *)arg;
	int	i;
	int	row;
	int	col;

	/* Clear screen */
	printf("\033[2J");
	printf("\033[0;0f");

	for (row = rows / 2, col = cols / 2, i = 0; i < 10; ++i) {
		new_snake_atom(row, col++);
	}	

	put_food(0);
	srand((unsigned)time(NULL));

	for (;;) {
		int	was;
		long	realdelay;
		
		was = advance();
		if (was == DIR_UP || was == DIR_DOWN) { 
			realdelay = delay + (delay / 2); 
		} else {
			realdelay = delay;
		}	
		usleep(realdelay);
	}	
	return NULL;
}



static int
n_getch(void) {
	struct termios	t2;
	int		ch;
	static int	inited;

	if (!inited) {
		(void) tcgetattr(0, &oldterm);
		t2 = oldterm;
		t2.c_lflag &= ~(ICANON | ECHO);
		t2.c_cc[VMIN] = 1;
		t2.c_cc[VTIME] = 0;
		(void) tcsetattr(0, TCSANOW, &t2);
		atexit(restore_term);
		inited = 1;
	}
	ch = getchar();
#define DEBUG
#ifdef DEBUG
#if 0
	printf("\033[0;0f");
	printf("read char %d", ch);
#endif
	if (ch == 'q') {
		printf("\033[0;0f");
		printf("frow = %d, fcol = %d", food_row, food_col);
	}	
#endif	
	return ch;
}

static int
opposite_direction(int old, int new) {
	switch (old) {
	case DIR_LEFT:
		return new == DIR_RIGHT;
	case DIR_RIGHT:
		return new == DIR_LEFT;
	case DIR_UP:
		return new == DIR_DOWN;
	case DIR_DOWN:
		return new == DIR_UP;
	}	
	abort();
}	

static void *
keyboard_thread(void *arg) {
	int	ch;
	int	newdir;

	(void) arg;
	
	while ((ch = n_getch()) != EOF) {
		switch (ch) {
		case 'a': /* left */
			newdir = DIR_LEFT;
			break;
		case 'd': /* right */	
			newdir = DIR_RIGHT;
			break;
		case 'w': /* up */
			newdir = DIR_UP;
			break;
		case 's': /* down */	
			newdir = DIR_DOWN;
			break;
		default:
			newdir = -1;
		}
		if (newdir != -1) {
			pthread_mutex_lock(&dirmut);
			if (!opposite_direction(direction, newdir)) {
				direction = newdir;
				needupdate = 1;
				while (needupdate) {
					pthread_cond_wait(&dircond, &dirmut);
				}	
			}	
			pthread_mutex_unlock(&dirmut);
		}	
	}
	return NULL;
}	

static void
get_rows_cols(void) {
	int	cmd = -1;

#ifndef _AIX /* XXX -D_XOPEN_SOURCE=500 is apparently needed for pthread.h but breaks winsize.. -D_ALL_SOURCE breaks too */
#if defined TIOCGWINSZ || defined TIOCGSIZE
	struct winsize	ws;
#    if defined TIOCGWINSZ
	cmd = TIOCGWINSZ;
#    else
	cmd = TIOCGSIZE;
#    endif
	if (ioctl(0, cmd, &ws) != -1) {
		rows = ws.ws_row;
		cols = ws.ws_col;
	} else {
		cmd = -1;
	}
#endif
#endif
	if (cmd == -1) {
		/* Assume 80x25 */
		rows = 25;
		cols = 80;
	}
}	



int
driver(int delay) {
	pthread_t	p1;
	pthread_t	p2;
	int		rc;

#ifdef SIGWINCH
	(void) signal(SIGWINCH, sigwinch);
#endif	
	setbuf(stdout, NULL);

	get_rows_cols();

	if ((rc = pthread_create(&p1, NULL, display_thread, &delay)) != 0
	|| (rc = pthread_create(&p2, NULL, keyboard_thread, NULL)) != 0) {
		(void) fprintf(stderr, "pthread_create: %s\n", strerror(rc));
		return EXIT_FAILURE;
	}
	pthread_join(p1, NULL);
	pthread_join(p2, NULL);
	return EXIT_FAILURE;
}

