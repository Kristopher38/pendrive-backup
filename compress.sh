#!/bin/sh
# argumenty: $1 - katalog do skompresowania
if [ "$#" -ne 1 ]; then
	echo "Usage: $0 directory_to_compress" >&2
	exit 1
fi
if ! [ -e "$1" ]; then
	echo "$1 not found" >&2
	exit 1
fi
if ! [ -d "$1" ]; then
	echo "$1 not a directory" >&2
	exit 1
fi

tar -cvf /tmp/backup.tar $1
cd $1
rm -rf *
cd ..
cp /tmp/backup.tar $1
rm /tmp/backup.tar
exit
