#!/bin/sh
ftp -n $1 $2 <<END_SCRIPT
quote USER $3
quote PASS $4
put $5 $6
quit
END_SCRIPT
exit
