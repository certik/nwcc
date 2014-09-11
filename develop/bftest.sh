#!/bin/sh

if ! test -d bftest; then
	if ! mkdir bftest; then
		exit 1
	fi
fi

if ! cd bftest; then
	exit 1
fi

cp ../bfgen.c .
gcc bfgen.c -o bfgen  # XXX


CNT=0
while test $CNT != $1; do
	./bfgen >x.c
	gcc x.c -o x
	./x >gcc.out

	if ! nwcc x.c -o x; then
		echo x.c failed
		exit 1
	fi
	./x >nwcc.out

	if test "`diff nwcc.out gcc.out`" != ""; then
		echo x.c failed
		diff nwcc.out gcc.out
		exit 1
	fi

	CNT=`expr $CNT + 1`
done

