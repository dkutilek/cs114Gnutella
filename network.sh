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

DELAY=0
CLIENT=0
FIRSTPORT="11111"
FIRSTPORTSET=0
if [ -n "$2" ]; then
 if [ "$2" = "-d" ]; then
   DELAY=1
 elif [ "$2" = "-c" ]; then
  CLIENT=1
 elif [ "$2" -lt 0 ]; then
   echo "<port> must be positive, using default port 11111"
 else
   FIRSTPORT=$2
   FIRSTPORTSET=1
 fi
fi

if [ -n "$3" ]; then
 if [ "$3" = "-d" ]; then
   DELAY=1
 elif [ "$3" = "-c" ]; then
   CLIENT=1
 elif [ "$FIRSTPORTSET" -eq 0 ]; then
  if [ "$3" -lt 0 ]; then
   echo "<port> must be positive, using default port 11111"
  else
   FIRSTPORT=$3
   FIRSTPORTSET=1
  fi
 fi
fi

if [ -n "$4" ]; then
 if [ "$4" = "-d" ]; then
   DELAY=1
 elif [ "$4" = "-c" ]; then
   CLIENT=1
 elif [ "$FIRSTPORTSET" -eq 0 ]; then
  if [ "$4" -lt 0 ]; then
   echo "<port> must be positive, using default port 11111"
  else
   FIRSTPORT=$4
   FIRSTPORTSET=1
  fi
 fi
fi

rm -f "done"

# Control while loop
OPTIONS="Build Destroy Exit"
while true; do
    # List options
    X=1
    for i in $OPTIONS; do
      # Display "User Controlled Node" instead of just "User"
      if [ "$i" = "Build" ]; then
        i="Build network and enter user node"
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
     rm -f "done"
     if [ "$CLIENT" -eq 1 ]; then
       ./gnutella --listen=$PORT -c &
     else
       ./gnutella --listen=$PORT &
     fi
     sleep 2
     let "PORT += 2"
     let "NUMNODES -= 1"
     let "COUNT += 1"
     while [ "$NUMNODES" != "0" ];
       do
         let "MODCOUNT = COUNT % 2"
         if [ "$MODCOUNT" -eq 0 ]; then
           let "BOOTPORT += 2"
         fi
         if [ "$DELAY" -eq 1 ]; then
           if [ "$MODCOUNT" -eq 1 ]; then
             if [ "$CLIENT" -eq 1 ]; then
               ./gnutella --listen=$PORT --bootstrap=127.0.0.1:$BOOTPORT -c &
             else
               ./gnutella --listen=$PORT --bootstrap=127.0.0.1:$BOOTPORT &
             fi
           else
             if [ "$CLIENT" -eq 1 ]; then
               ./gnutella --listen=$PORT --bootstrap=127.0.0.1:$BOOTPORT -d -c &
             else
               ./gnutella --listen=$PORT --bootstrap=127.0.0.1:$BOOTPORT -d &
             fi
           fi
         else
           if [ "$CLIENT" -eq 1 ]; then
             ./gnutella --listen=$PORT --bootstrap=127.0.0.1:$BOOTPORT -c &
           else
             ./gnutella --listen=$PORT --bootstrap=127.0.0.1:$BOOTPORT &
           fi
         fi
         DONEGREP=`ls | grep "done"`
         echo `ls | grep "done"`
         while [ "$DONEGREP" != "done" ];
           do
             DONEGREP=`ls | grep "done"`;
         done
         rm -f "done"
         sleep 2
         let "PORT += 2"
         let "NUMNODES -= 1"
         let "COUNT += 1"
     done
     echo `ps -e | grep gnutella | awk '{print $1}'`
     OPTIONS="Destroy Exit"
     echo "Network built, entering user node!"
     ./gnutella --listen=$PORT --bootstrap=127.0.0.1:$BOOTPORT -u

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

    # Exit script
    elif [ "$opt" = "Exit" ]; then
     exit

    else
     echo "Bad option"
    fi
done
