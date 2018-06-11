#!/bin/sh
module="uframe"
mode="664"
dir="/dev/uframe"
# invoke insmod with all arguments we got
# and use a pathname, as newer modutils don't look in . by
/sbin/insmod ./udriver.ko $* || exit 1
mkdir -p $dir
# remove stale nodes
rm -f $dir/command
rm -f  $dir/bulk    
rm -f  $dir/interrupt

major=`awk "\\$2==\"$module\" {print \\$1}" /proc/devices`

mknod $dir/command c $major 0
mknod $dir/bulk c $major 1 
mknod $dir/interrupt c $major 2

# give appropriate group/permissions, and change the group.
# Not all distributions have staff, some have "wheel" instead.
group="staff"
grep -q '^staff:' /etc/group || group="wheel"
chgrp $group $dir/command
chmod $mode $dir/command
chgrp $group  $dir/bulk
chmod $mode  $dir/bulk
chgrp $group $dir/interrupt
chmod $mode $dir/interrupt
