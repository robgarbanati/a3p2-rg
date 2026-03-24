#!/bin/bash

if [ "$#" -ne 2 ]; then
    echo "Error: Expected 2 arguments, got $#"
    exit 1
fi

writefile="$1"
writestr="$2"

dir=$(dirname "$writefile")
mkdir -p "$dir"

if ! echo "$writestr" > "$writefile"; then
    echo "Could not write $writestr to file $writefile"
    exit 1
fi
