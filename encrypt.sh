#!/bin/sh
# argumenty: $1 - katalog w którym znajduje się archiwum do zaszyfrowania

if [ "$#" -ne 1 ]; then
	echo "Usage: $0 directory_with_archive_to_encrypt" >&2
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
if ! [ -f "$1/backup.tar" -o -r "$1/backup.tar" ]; then
	echo "$1/backup.tar file doesn't exist or no permission to read"
	exit 1
fi
if ! [ -f "encpass" -a -r "encpass" ]; then
	echo "encpass file (file with encryption password) doesn't exist or no permission to read"
	exit 1
fi

aescrypt -e -p $(cat encpass) $1/backup.tar
rm $1/backup.tar
exit
