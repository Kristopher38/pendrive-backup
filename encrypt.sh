#!/bin/sh
# argumenty: $1 - katalog w którym znajduje się archiwum do zaszyfrowania

if [ "$#" -ne 1 ]; then		# sprawdzenie czy ilość argumentów jest poprawna
	echo "Usage: $0 directory_with_archive_to_encrypt" >&2
	exit 1
fi
if ! [ -e "$1" ]; then		# sprawdzenie czy katalog istnieje
	echo "$1 not found" >&2
	exit 1
fi
if ! [ -d "$1" ]; then		# sprawdzenie czy ścieżka do katalogu jest katalogiem
	echo "$1 not a directory" >&2
	exit 1
fi
if ! [ -f "$1/backup.tar" -o -r "$1/backup.tar" ]; then		# sprawdzenie czy plik archiwum istnieje i mamy do niego prawa odczytu
	echo "$1/backup.tar file doesn't exist or no permission to read"
	exit 1
fi
if ! [ -f "secret.key" -a -r "secret.key" ]; then			# sprawdzenie czy plik z kluczem (zaszyfrowanym hasłem) istnieje i mamy do niego prawa odczytu
	echo "secret.key file (encrypted file with password) doesn't exist or no permission to read"
	exit 1
fi

aescrypt -e -k secret.key $1/backup.tar		# zaszyfruj archiwum z kluczem secret.key
AES_CODE=$?
rm $1/backup.tar							# usuń niezaszyfrowane archiwum.tar
exit $AES_CODE								# wyjdź ze status kodem aescrypta
