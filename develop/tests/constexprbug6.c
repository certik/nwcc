#include <stdio.h>

int main() { 
	int n;

	n = 1;
	if ( (0 || (n))) {
		puts("gnu");
	}
	if ( !(0 || (n))) {
		puts("well");
	} else {
		puts("gnugnu");
	}

	n = 0;
	if ( (0 || (n))) {
		puts("false");
	} else {
		puts("hurd");
	}


	if ( !(0 || (n))) {
		puts("absurd");
	} else {
		puts("foo");
	}

	n = 1;
	if ( ((n) || 0)) {
		puts("gnu");
	}
	if ( !((n) || 0)) {
		puts("well");
	} else {
		puts("gnugnu");
	}

	n = 0;
	if ( ((n) || 0)) {
		puts("false");
	} else {
		puts("hurd");
	}


	if ( !((n) || 0)) {
		puts("absurd");
	} else {
		puts("foo");
	}
}

