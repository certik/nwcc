int
main(void) {
	long long foo = 123;
	long long upper_set = 123ll << 32;
	long long both_set = (456ll << 32) | 123; 

	printf("%lld, %lld, %lld\n", foo, upper_set, both_set);
	if (foo != 456) {
		puts("good0");
	} else {
		puts("bug0");
	}

	if (foo == 456) {
		puts("bug1");
	} else {
		puts("good1");
	}

	if (foo == 123) {
		puts("good2");
	} else {
		puts("bug2");
	}
	
	if (foo != 123) {
		puts("bug3");
	} else {
		puts("good3");
	}	

	if (456 > foo) {
		puts("good4");
	} else {
		puts("bug4");
	}	

	if (foo >= 120) {
		puts("good5");
	} else {
		puts("bug5");
	}	

	if (foo < 16) {
		puts("bad6");
	} else {
		puts("good6");
	}	

	if (foo == upper_set) {
		puts("bug7");
	} else {
		puts("good7");
	}	

	if (foo == both_set) {
		puts("bug8");
	} else {
		puts("good8");
	}	

	if (foo <= both_set) {
		puts("good9");
	} else {
		puts("bad9");
	}	

	foo = 456;
	if (foo > both_set) {
		puts("bug10");
	} else {
		puts("good10");
	}	
	foo = both_set;
	if (foo != both_set) {
		puts("bug11");
	} else {
		puts("good11");
	}	
	--foo;
	if (foo >= both_set) {
		puts("bug12");
	} else {
		puts("good12");
	}	
	if (foo) {
		puts("good13");
	} else {
		puts("bad13");
	}
	foo = 123ll << 32;
	if (foo == 0) {
		puts("bad to the maximum");
	} else {
		puts("good");
	}	
	if (!foo) {
		puts("bad14");
	} else {
		puts("good14");
	}	
	return 0;
}

