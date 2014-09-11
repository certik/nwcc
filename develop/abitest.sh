#!/bin/sh

if ! test -d abitest; then
	if ! mkdir abitest; then
		exit 1
	fi
fi

if ! cd abitest; then
	exit 1
fi

cp ../abigen.c .
cp ../nwcc1 .
gcc abigen.c -o abigen  # XXX


CNT=0
MAX=$1

shift

while test $CNT != $MAX; do
	if ! ./abigen $@ >x.c; then
		echo abigen error
		exit 1
	fi
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

