#!/bin/sh
# Writer script for assignment 1
# Author: Juan Barragan

if [ $# -lt 2 ]; then
        echo "ERROR: Invalid Number of Arguments."
        echo "Total number of arguments should be 2."
        echo "The order of the arguments should be:"
        echo "  1)File Path."
        echo "  2)String to be written in the specified file path."
        exit 1
fi

writefile=$1
writestr=$2

#remove directory if already there
rm -fr ${writefile}

#create directory
mkdir -p "$(dirname "${writefile}")"

#error statement if unable to make directory
if [ $? -ne 0 ]; then
	echo "ERROR: Unable to make directory"
	exit 1
fi

#create file
touch ${writefile}

#error statement if unable to make file
if [ $? -ne 0 ]; then
        echo "ERROR: Unable to make file"
        exit 1
fi

#write to file
echo ${writestr} > ${writefile}
