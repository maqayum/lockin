#!/bin/sh

. scripts/config;

if [ $# -gt 0 ];
then
    LOCKS="$@";
fi;

UNAMEN=$(uname -n);

if [ $UNAMEN = "lpdpc4" ];
then
    printf "";
fi;

if [ $UNAMEN = "lpdpc34" ];
then
    printf "";
fi;


usage()
{
    echo "$0 [-v] [-s suffix]";
    echo "    -v             verbose";
    echo "    -s suffix      suffix the executable with suffix";
}


USUFFIX="";
VERBOSE=0;
 while getopts "hs:v" OPTION
 do
      case $OPTION in
          h)
	      usage;
              exit 1
              ;;
          s)
              USUFFIX="_$OPTARG"
	      echo "Using suffix: $USUFFIX"
              ;;
          v)
              VERBOSE=1
              ;;
          ?)
	      usage;
              exit;
              ;;
      esac
 done

 all="stress_one_in stress_test_in stress_queued_in stress_latency_in stress_ldi_in stress_phase_in";

for lock in $LOCKS
do
    echo "Building: $lock";
    touch Makefile;
    if [ $VERBOSE -eq 1 ]; 
    then
	PAUSE_IN=3 LOCK_IN=$lock $MAKE $all
    else
	PAUSE_IN=3 LOCK_IN=$lock $MAKE $all  > /dev/null;
    fi

    for e in $all;
    do
	mv ${e} ${e}_${lock};
    done;
done;
