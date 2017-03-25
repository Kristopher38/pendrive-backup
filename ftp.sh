#!/bin/sh
# Skrypt odpowiedzialny za wysyłanie skompresowanej (i opcjonalnie zaszyfrowanej) kopii zapasowej na serwer FTP. Znajduje się na pamięci masowej wraz z programem i jest uruchamiany przez program poleceniem system() podczas zamykania systemu po wykonaniu skryptów compress.sh i/lub encrypt.sh. Wykonuje dwa połączenia z serwerem FTP użytkownika – w pierwszym wysyła kopię zapasową na serwer i przy okazji parsuje listę wszystkich plików, a w drugim usuwa starsze kopie zapasowe aby zwolnić miejsce na serwerze FTP. Maksymalna ilość kopii która może znajdować się na serwerze FTP określana jest w pliku /etc/pbackup/keep-backups. Wykonanie podwójnego połączenia jest wymagane ze względu na specyfikę działania skryptów.

# argumenty: $1 - adres serwera, $2 - port serwera, $3 - nazwa użytkownika, $4 - hasło użytkownika, $5 - katalog backupu

FILENAME=$(cat /etc/pbackup/lastfilename)	# pobierz nazwę aktualnie obsługiwanego archiwum

if [ "$#" -ne 5 ]; then	# sprawdzenie czy ilość argumentów jest poprawna
	echo "Usage: $0 address port username password backup_directory"
	exit 1
fi

if ! [ -e "$5" ]; then	# sprawdzenie czy katalog backupu istnieje
	echo "$5 not found" >&2
	exit 1
fi

if ! [ -d "$5" ]; then	# sprawdzenie czy ścieżka do katalogu jest katalogiem
	echo "$5 not a directory" >&2
	exit 1
fi

if ! [ -f $5/$FILENAME -a -r $5/$FILENAME ] # sprawdź czy plik .tar.aes istnieje, jest zwykłym plikiem i mamy możliwość odczytu
then
	echo "Couldn't find backup archive"
	exit 1
fi

# pierwsze połącznie z serwerem ftp
# zaloguj się na serwer ftp z podanymi parametrami, zapisując logi do pliku /var/log/pbackup-ftp.log
# prześlij plik kopii zapasowej na serwer
# pobierz listę wszystkich plików i zapisz ją w pliku /etc/pbackup/ftpfiles
# zakończ połączenie
FTPLOG=/var/log/pbackup-ftp.log
ftp -inv $1 $2 <<END_FTP > $FTPLOG 2>&1
quote USER $3
quote PASS $4
put $5/$FILENAME $FILENAME
ls -t /etc/pbackup/ftpfiles
close
quit
END_FTP

# sprawdzenie czy połączenie się udało
FTP_TRANSFER_OK="226"
FTP_LOGIN_FAIL="530"
FTP_NOTCONNECTED="Not connected"
if fgrep -q "$FTP_TRANSFER_OK" $FTPLOG ;then		# sprawdź czy transfer się udał
	echo "FTP backup archive transfer successfull"
elif fgrep -q "$FTP_LOGIN_FAIL" $FTPLOG ;then		# sprawdź czy wystąpił błąd podczas logowania
	echo "FTP transfer failed: Login authentication failed"
	exit 2
elif fgrep -q "$FTP_NOTCONNECTED" $FTPLOG ;then		# sprawdź czy wystąpił inny błąd (brak połączenia z serwerem) i wypisz logi z pliku /var/log/pbackup-ftp.log
	echo "FTP transfer failed, log: \n"$(sed '/Not connected/Q' $FTPLOG)
	exit 2
else												# nieznany błąd
	echo "FTP transfer failed: unknown reason, see $FTPLOG for details"
	exit 2
fi

KEEP_BACKUPS=`cat /etc/pbackup/keep-backups`	# pobierz ile maksymalnie kopii zapasowych może być przechowywanych na serwerze 
TO_DELETE=$(grep -oE '[^ ]+$' /etc/pbackup/ftpfiles | sed -e "1,"$KEEP_BACKUPS"d" | tr "\n" " ")	# utwórz listę starych kopii do usunięcia na podstawie utworzonej wcześniej listy wszystkich plików na serwerze i ilości maksymalnie przechowywanych kopii zapasowych

# drugie połączenie z serwerem ftp
# zaloguj się na serwer ftp z podanymi parametrami, zapisując logi do pliku /var/log/pbackup-ftp.log
# usuń wszystkie pliki z listy starych kopii zapasowych
# zakończ połączenie
ftp -inv $1 $2 <<END_FTP >> $FTPLOG 2>&1
quote USER $3
quote PASS $4
mdelete $TO_DELETE
close
quit
END_FTP
echo "Removed older backups from ftp server"
