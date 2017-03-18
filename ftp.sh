#!/bin/sh
# argumenty: $1 - adres serwera, $2 - port serwera, $3 - nazwa użytkownika, $4 - hasło użytkownika, $5 - katalog backupu

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

# sprawdzenie czy plik jest zaszyfrowany (szyfrowanie jest włączone), czy jest tylko archiwum (szyfrowanie jest wyłączone)
if [ -f $5/backup.tar.aes -a -r $5/backup.tar.aes ] # sprawdź czy plik .tar.aes istnieje, jest zwykłym plikiem i mamy możliwość odczytu
then
	FILENAME="backup.tar.aes"
elif [ -f $5/backup.tar -a -r $5/backup.tar ] # sprawdź czy plik .tar istnieje, jest zwykłym plikiem i mamy możliwość odczytu 
then
	FILENAME="backup.tar"
else
	echo "Couldn't find backup archive"
	exit 1
fi

# przeprowadź transfer pliku logując się z podanymi parametrami, zapisując logi do pliku /tmp/ftplog
FTPLOG=/var/log/pbackup-ftp.log
ftp -nv $1 $2 <<END_FTP > $FTPLOG 2>&1
quote USER $3
quote PASS $4
put $5/$FILENAME $FILENAME
close
quit
END_FTP

FTP_TRANSFER_OK="226"
FTP_LOGIN_FAIL="530"
FTP_NOTCONNECTED="Not connected"
if fgrep -q "$FTP_TRANSFER_OK" $FTPLOG ;then		# sprawdź czy transfer się udał
	echo "FTP backup archive transfer successfull"
	exit 0
elif fgrep -q "$FTP_LOGIN_FAIL" $FTPLOG ;then		# sprawdź czy wystąpił błąd podczas logowania
	echo "FTP transfer failed: Login authentication failed"
	exit 2
elif fgrep -q "$FTP_NOTCONNECTED" $FTPLOG ;then		# sprawdź czy wystąpił inny błąd (brak połączenia z serwerem) i wypisz logi
	echo "FTP transfer failed, log: \n"$(sed '/Not connected/Q' $FTPLOG)
	exit 2
else												# nieznany błąd
	echo "FTP transfer failed: unknown reason, see $FTPLOG for details"
	exit 2
fi
