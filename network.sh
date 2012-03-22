#!/bin/bash

# Usage notification
if [ -z "$1" ]; then
 echo "usage: $0 <num nodes>"
 exit
fi

# Input check
if [ "$1" -lt 0 ]; then
 echo "<num nodes> must be positive"
 exit
fi

# Port value check
FIRSTPORT="11111"
if [ -n "$2" ]; then
  if [ "$2" -lt 0 ]; then
    echo "<port> must be positive, using default port 11111"
  else
    FIRSTPORT=$2
  fi
fi

# Control while loop
OPTIONS="Build Destroy Exit"
while true; do
    # List options
    X=1
    for i in $OPTIONS; do
      # Display "User Controlled Node" instead of just "User"
      if [ "$i" = "User" ]; then
        i="User Controlled Node"
      fi
      echo "$X) $i"
      let "X += 1"
    done
    echo -n "#? "
    read opt

    # Read value
    X=1
    FOUND="0"
    for i in $OPTIONS; do
      if [ "$opt" = "$X" ]; then
        opt=$i
        FOUND="1"
        break
      fi
      let "X += 1"
    done
    if [ "$FOUND" = "0" ]; then
      opt="Bad Option"
    fi


    # Build network and allow query option
    if [ "$opt" = "Build" ]; then
     echo "Building network..."
     NUMNODES=$1
     PORT=$FIRSTPORT
     COUNT=0
     MODCOUNT=0
     BOOTPORT=$PORT
     ./gnutella --listen=$PORT &
     let "PORT += 2"
     let "NUMNODES -= 1"
     let "COUNT += 1"
     while [ "$NUMNODES" != "0" ];
       do
         let "MODCOUNT = COUNT % 5"
         if [ "$MODCOUNT" -eq 0 ]; then
           let "BOOTPORT += 2"
         fi
         ./gnutella --listen=$PORT --bootstrap=127.0.0.1:$BOOTPORT &
         let "PORT += 2"
         let "NUMNODES -= 1"
         let "COUNT += 1"
     done
     echo `ps -e | grep gnutella | awk '{print $1}'`
     OPTIONS="Destroy User Exit"
     echo "Network built!"

    # Destroy network and remove query option
    elif [ "$opt" = "Destroy" ]; then
     echo "Destroying network..."
     PIDS=`ps -e | grep gnutella | awk '{print $1}'`;
     echo $PIDS
     for i in $PIDS;
       do
         kill -9 $i
     done
     OPTIONS="Build Destroy Exit"
     PORT=$FIRSTPORT
     BOOTPORT=$PORT
     COUNT=0
     NUMNODES=$1
     MODCOUNT=0
     echo "Network destroyed!"

    # Start user control Gnutella node
    elif [ "$opt" = "User" ]; then
      ./gnutella --listen=$PORT --bootstrap=127.0.0.1:$BOOTPORT -u

    # Exit script
    elif [ "$opt" = "Exit" ]; then
     exit

    else
     echo "Bad option"
    fi
done
