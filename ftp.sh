#!/bin/sh
# argumenty: $1 - adres serwera, $2 - port serwera, $3 - nazwa użytkownika, $4 - hasło użytkownika, $5 - katalog backupu

FILENAME=$(cat /etc/pbackup/lastfilename)

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
if ! [ -f $5/$FILENAME -a -r $5/$FILENAME ] # sprawdź czy plik .tar.aes istnieje, jest zwykłym plikiem i mamy możliwość odczytu
then
	echo "Couldn't find backup archive"
	exit 1
fi

# przeprowadź transfer pliku logując się z podanymi parametrami, zapisując logi do pliku /tmp/ftplog
FTPLOG=/var/log/pbackup-ftp.log
ftp -inv $1 $2 <<END_FTP > $FTPLOG 2>&1
quote USER $3
quote PASS $4
put $5/$FILENAME $FILENAME
ls -t /etc/pbackup/ftpfiles
close
quit
END_FTP

FTP_TRANSFER_OK="226"
FTP_LOGIN_FAIL="530"
FTP_NOTCONNECTED="Not connected"
if fgrep -q "$FTP_TRANSFER_OK" $FTPLOG ;then		# sprawdź czy transfer się udał
	echo "FTP backup archive transfer successfull"
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

KEEP_BACKUPS=`cat /etc/pbackup/keep-backups`
TO_DELETE=$(grep -oE '[^ ]+$' /etc/pbackup/ftpfiles | sed -e "1,"$KEEP_BACKUPS"d" | tr "\n" " ")
ftp -inv $1 $2 <<END_FTP >> $FTPLOG 2>&1
quote USER $3
quote PASS $4
mdelete $TO_DELETE
close
quit
END_FTP
echo "Removed older backups from ftp server"
