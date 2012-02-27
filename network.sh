#!/bin/bash
if [ -z "$1" ]; then
 echo "usage: $0 <num nodes>"
 exit
fi

if [ "$1" -lt 0 ]; then
 echo "<num nodes> must be positive"
 exit
fi

PORT="11111"
if [ -n "$2" ]; then
  if [ "$2" -lt 0 ]; then
    echo "<port> must be positive, using default port 11111"
  else
    PORT=$2
  fi
fi

NUMNODES=$1
COUNT=0
MODCOUNT=0
BOOTPORT=$PORT
OPTIONS="Build Destroy Exit"
select opt in $OPTIONS; do
    if [ "$opt" = "Build" ]; then
     echo "Building network..."
     ./gnutella $PORT &
     let "PORT += 1"
     let "NUMNODES -= 1"
     let "COUNT += 1"
     while [ "$NUMNODES" != "0" ];
       do
         let "MODCOUNT = COUNT % 5"
         if [ "$MODCOUNT" -eq 0 ]; then
           let "BOOTPORT += 1"
         fi
         ./gnutella $PORT 127.0.0.1 $BOOTPORT &
         let "PORT += 1"
         let "NUMNODES -= 1"
         let "COUNT += 1"
     done
     echo `ps -e | grep gnutella | awk '{print $1}'`
     echo "Network built!"
    elif [ "$opt" = "Destroy" ]; then
     echo "Destroying network..."
     PIDS=`ps -e | grep gnutella | awk '{print $1}'`;
     echo $PIDS
     for i in $PIDS;
       do
         kill -9 $i
     done
     echo "Network destroyed!"
    elif [ "$opt" = "Exit" ]; then
     exit
    else
     clear
     echo "Bad option"
    fi
done
