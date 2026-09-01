#!/bin/sh
# Writer script for assignment 1
# Author: Juan Barragan

#error check for arguments
if [ $# -lt 2 ]; then
	echo "ERROR: Invalid Number of Arguments."
	echo "Total number of arguments should be 2."
	echo "The order of the arguments should be:"
	echo "	1)File Directory Path."
	echo "	2)String to be searched in the specified directory path."
	exit 1
fi

#assign arguments
filesdir=$1
searchstr=$2

#error check that directory is real
if [ ! -d "$filesdir" ]; then
        echo "ERROR: File directory not found"
        exit 1
fi

totalfiles=$(find "${filesdir}" -type f | wc -l)
matches=$(grep -r "${searchstr}" "${filesdir}" | wc -l)

echo "The number of files are ${totalfiles} and the number of matching lines are ${matches}"
