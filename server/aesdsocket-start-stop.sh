#!/bin/sh

DAEMON=/usr/bin/aesdsocket
NAME=aesdsocket

case "$1" in
    start)
        echo -n "Starting $NAME: "
        start-stop-daemon -S -n $NAME -a $DAEMON -- -d
        echo "OK"
        ;;
    stop)
        echo -n "Stopping $NAME: "
        start-stop-daemon -K -n $NAME -s TERM
        echo "OK"
        ;;
    restart)
        "$0" stop
        "$0" start
        ;;
    *)
        echo "Usage: $0 {start|stop|restart}"
        exit 1
        ;;
esac

exit 0
