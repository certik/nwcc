#!/bin/sh

if test "$1" = ""; then 
	echo Please supply a directory to diff with
	exit 1
fi

oldcwd=`pwd`
if ! cd $1; then
	exit 1
fi	
echo "Diffing all .c and .h files in $1 with CWD's versions..."

for i in `ls *.[chs]`; do
	if ! test -f "$oldcwd/$i"; then
		continue
	fi
	STUFF=`diff $oldcwd/$i ./$i`
	if test "$STUFF" != ""; then
		if test "$2" = "--verbose"; then
			echo "$STUFF"
		else	
			echo "$i differs"
		fi	
	fi
done
cd $oldcwd 

