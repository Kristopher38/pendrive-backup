#!/bin/bash
user=$(whoami);
last_mount="";
while :
do
mount_list="/etc/mtab";
while read -r line;
do
 if [[ $line =~ ^/dev/+ ]]; then
  IFS=' ' read -a mount_device <<< "$line";
   if [[ ${mount_device[1]} != "/" ]]; then
     if [[ ${mount_device[2]} == "ext4" ]] || [[ ${mount_device[2]} == "fuseblk" ]]; then
    if [ -f "${mount_device[1]}/pbackup" ]; then 
      "${mount_device[1]}/pbackup" "${mount_device[1]}/" "/";
      exit;
    fi
     fi
   fi
 fi
done < "$mount_list"
if [[ $last_mount != "" ]]; then
	umount $last_mount;
fi
last_mount="";
sleep 5;
lsblk | \
while read -r line;
do
 if [[ $line =~ ^└─+ ]]; then
    IFS=' ' read -a device_to_mount <<< "$line";
     if [[ ${device_to_mount[6]} == "" ]] && [[ ${device_to_mount[2]} == "1" ]] && [[ ${device_to_mount[5]} == "part" ]]; then
        if [[ ${device_to_mount[3]} == *"M"* ]]; then
           org_data=${device_to_mount[0]};
       to_replace="";
       disk_path="/dev/${org_data/└─/$to_replace}";
       mkdir "/media/$user";
       mkdir "/media/$user/backup_pendrive";
       mount $disk_path;
	   last_mount=$disk_path;
       sleep 1;
        fi
     fi
 fi
done
done
