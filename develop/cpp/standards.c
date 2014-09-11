#include "standards.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static struct std_info {
	int	value;
	char	*name;
} standards[] = {
	{ ISTD_C89, "c89" },
	{ ISTD_C99, "c99" },
	{ ISTD_GNU89, "gnu89" },
	{ ISTD_GNU99, "gnu99" },
	{ 0, NULL }
};

static struct std_info	*selected_istd;
int			stdflag = ISTD_NONE;


int
option_to_std(const char *opt) {
	int	i;

	for (i = 0; standards[i].name != NULL; ++i) {
		if (strcmp(standards[i].name, opt) == 0) {
			selected_istd = &standards[i];
			return selected_istd->value;
		}
	}
	(void) fprintf(stderr, "Warning: Unknown standard `%s', using default\n", opt);
	return ISTD_NONE;
}

int
set_default_std(void) {
	int	i;

	for (i = 0; standards[i].value != ISTD_DEFAULT; ++i) {
		if (standards[i].name == NULL) {
			(void) fprintf(stderr, "BUG: No default standard\n");
			exit(EXIT_FAILURE);
		}
	}
	selected_istd = &standards[i];
	return selected_istd->value;
}

char *
get_selected_std_name(void) {
	return selected_istd->name;
}

int
using_strict_iso_c(void) {
	if (stdflag == ISTD_C89 || stdflag == ISTD_C99) {
		return 1;
	} else {
		return 0;
	}
}


