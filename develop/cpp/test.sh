#!/bin/sh

if ! cd tests; then
	exit 1
fi	
cp ../nwcpp .
INPUT="Some stuff for input"

for i in `ls *.c`; do
	printf "Trying $i ... "
	if ! ./nwcpp $i >nwcpp.out.i; then
		echo NWCPP ERROR
		continue
	fi
	if ! gcc nwcpp.out.i 2>/dev/null; then
		echo INVALID CODE
		continue
	fi
	rm nwcpp.out.i
	echo $INPUT | ./a.out >nwcpp.out
	gcc $i 2>/dev/null
	echo $INPUT | ./a.out >gcc.out
	DIFF=`diff nwcpp.out gcc.out`
	rm -f nwcpp.out gcc.out a.out
	if test "$DIFF" != ""; then
		echo BAD CODE - "$DIFF"
	else
		echo OK
	fi
done	

cd ..
