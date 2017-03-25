#!/bin/sh
# Skrypt odpowiedzialny za kompresję używanych podczas danej sesji plików. Znajduje się na pamięci masowej wraz z programem i jest uruchamiany przez program poleceniem system() podczas zamykania systemu po skopiowaniu plików na pamięć masową. Używa programu command-line „tar” aby skompresować katalog ze skopiowanymi na pamięć masową plikami. W nazwie skompresowanego archiwum określona jest data i czas wykonania kopii zapasowej w formacie: backup_dzień-miesiąc-rok_godzina-minuta-sekunda.tar. Usuwa starsze kopie zapasowe aby zwolnić miejsce na pamięci masowej. Maksymalna ilość kopii która może znajdować się na pamięci masowej określana jest w pliku /etc/pbackup/keep-backups.

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

FILENAME=$(date +backup_%d-%m-%y_%H-%M-%S.tar)								# utwórz nazwę pliku w formacie backup_dzień-miesiąc-rok_godzina-minuta-sekunda.tar
echo $FILENAME > "/etc/pbackup/lastfilename"								# zapisz do pliku nazwę skompresowanego archiwum

mkdir $1/../temp >> /dev/null 2>&1  										# utwórz katalog tymczasowy na stare archiwa
cd $1 >> /dev/null 2>&1														# przejdź do katalogu z backupami
find -name "backup_[0-9][0-9]-[0-9][0-9]-[0-9][0-9]_[0-9][0-9]-[0-9][0-9]-[0-9][0-9].tar" -exec mv {} -t $1/../temp \; 		# przenieś do niego poprzednie backupy
find -name "backup_[0-9][0-9]-[0-9][0-9]-[0-9][0-9]_[0-9][0-9]-[0-9][0-9]-[0-9][0-9].tar.aes" -exec mv {} -t $1/../temp \; 	# i zaszyfrowane backupy też

tar --remove-files -cvf $1/../$FILENAME -C $1 .			# skompresuj katalog
if [ $? = 2 ]                                   		# sprawdź czy wystąpił błąd podczas kompresji
then
	echo "Fatal error occurred when compressing backup folder"
    exit 1
else
	echo "Successfully compressed files"
fi
mkdir $1 >> /dev/null 2>&1                              # utwórz katalog backupu bo został usunięty podczas kompresji
mv $1/../$FILENAME $1 >> /dev/null 2>&1                	# przenieś archiwum do tegoż katalogu
mv $1/../temp/* $1 >> /dev/null 2>&1					# przenieś stare archiwa z powrotem do katalogu backupu 
rmdir $1/../temp >> /dev/null 2>&1						# usuń katalog tymczasowy

KEEP_BACKUPS=`cat /etc/pbackup/keep-backups`			# odczytaj ile backupów mamy zachować z pliku
cd $1 >> /dev/null 2>&1									# przejdź do katalogu z backupami
ls -t | sed -e "1,"$KEEP_BACKUPS"d" | xargs -d '\n' rm >> /dev/null 2>&1	# usuń najstarsze backupy zachowując taką ilość jak $KEEP_BACKUPS
echo "Removed older backups"

exit 0
