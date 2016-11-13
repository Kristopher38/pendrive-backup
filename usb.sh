#!/bin/bash
while :
do
mount_list="/etc/mtab"
while read -r line;
do
 if [[ $line =~ ^/dev/+ ]]; then
  IFS=' ' read -a mount_device <<< "$line";
   if [[ ${mount_device[1]} != "/" ]]; then
     if [[ ${mount_device[2]} == "ext4" ]] || [[ ${mount_device[2]} == "fuseblk" ]]; then
	if [ -f "${mount_device[1]}/projekt.sh" ]; then
 	  "${mount_device[1]}/projekt.sh" ${mount_device[1]};
	  exit;
	fi
     fi
   fi
 fi
done < "$mount_list"
sleep 5
done