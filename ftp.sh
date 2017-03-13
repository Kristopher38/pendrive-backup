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

if ! [ -d "$5" ]; then	# sprawdzenie czy katalog backupu jest katalogiem
	echo "$5 not a directory" >&2
	exit 1
fi 

# sprawdzenie czy plik jest zaszyfrowany (szyfrowanie jest włączone), czy jest tylko archiwum (szyfrowanie jest wyłączone)
if [ -f $5/backup.tar.aes -a -r $5/backup.tar.aes ] # sprawdź czy plik istnieje, jest zwykłym plikiem i mamy możliwość odczytu
then
	FILENAME="backup.tar.aes"
elif [ -f $5/backup.tar -a -r $5/backup.tar ] # sprawdź czy plik istnieje, jest zwykłym plikiem i mamy możliwość odczytu 
then
	FILENAME="backup.tar"
else
	echo "Couldn't find backup archive"
	exit 1
fi

ftp -n $1 $2 <<END_SCRIPT
quote USER $3
quote PASS $4
put $5/$FILENAME $FILENAME
quit
END_SCRIPT
exit
