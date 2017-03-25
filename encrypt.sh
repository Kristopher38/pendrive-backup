#!/bin/sh
# argumenty: $1 - katalog w którym znajduje się archiwum do zaszyfrowania

FILENAME=$(cat /etc/pbackup/lastfilename)

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
if ! [ -f "$1/$FILENAME" -o -r "$1/$FILENAME" ]; then		# sprawdzenie czy plik archiwum istnieje i mamy do niego prawa odczytu
	echo "$1/$FILENAME file doesn't exist or no permission to read"
	exit 1
fi
if ! [ -f "/etc/pbackup/secret.key" -a -r "/etc/pbackup/secret.key" ]; then			# sprawdzenie czy plik z kluczem (zaszyfrowanym hasłem) istnieje i mamy do niego prawa odczytu
	echo "/etc/pbackup/secret.key file (encrypted file with password) doesn't exist or no permission to read"
	exit 1
fi

aescrypt -e -k /etc/pbackup/secret.key $1/$FILENAME		# zaszyfruj archiwum z kluczem secret.key
AES_CODE=$?
if [ "$AES_CODE" = 0 ]; then
	echo "Successfully encrypted files"
	rm $1/$FILENAME							# usuń niezaszyfrowane archiwum.tar
	echo "$FILENAME.aes" > "/etc/pbackup/lastfilename"
	chmod 644 "$1/$FILENAME.aes"
else
	echo "Failed encrypting files"
fi

exit $AES_CODE								# wyjdź ze status kodem aescrypta
