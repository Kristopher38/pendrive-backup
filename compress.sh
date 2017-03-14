#!/bin/sh
# argumenty: $1 - katalog do skompresowania
if [ "$#" -ne 1 ]; then			# sprawdzenie czy ilość argumentów jest poprawna
	echo "Usage: $0 directory_to_compress" >&2
	exit 1
fi
if ! [ -e "$1" ]; then			# sprawdzenie czy katalog istnieje
	echo "$1 not found" >&2
	exit 1
fi
if ! [ -d "$1" ]; then			# sprawdzenie czy ścieżka do katalogu jest katalogiem
	echo "$1 not a directory" >&2
	exit 1
fi

tar --exclude='*.tar' --exclude='*.tar.aes' -cvf /tmp/backup.tar $1		# zaszyfruj katalog, wykluczając wszelkie archiwa .tar i .tar.aes znajdujące się w katalogu
if [ $? = 2 ]					# sprawdź exit code tara
then
	echo "Fatal error occurred when compressing backup folder"
	exit 1
fi

# usuń zawartość która została skompresowana, przekopiuj plik tymczasowy na pamięć masową i usuń plik tymczasowy
cd $1
rm -rf *
cd ..
cp /tmp/backup.tar $1
rm /tmp/backup.tar
exit 0
