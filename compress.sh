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

rm $1/backup.tar $1/backup.tar.aes >> /dev/null 2>&1    # usuń wcześniejsze backupy
tar --remove-files -cvf $1/../backup.tar -C $1 .		# zaszyfruj katalog
mkdir $1 >> /dev/null 2>&1                              # utwórz katalog backupu bo został usunięty podczas kompresji
mv $1/../backup.tar $1                                  # przenieś archiwum do tegoż katalogu
if [ $? = 2 ]					                        # sprawdź exit code tara
then
	echo "Fatal error occurred when compressing backup folder"
	exit 1
fi

exit 0
