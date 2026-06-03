#!/bin/sh

if [ "$#" -ne 2 ]; then
    echo "Expected 2 arguments, but got $#"
    exit 1
fi

filesdir="$1"
searchstr="$2"

if [ ! -d "$filesdir" ]; then
    echo "$filesdir is not a directory"
    exit 1
fi

file_count=$(find "$filesdir" -type f | wc -l)

match_count=$(grep -r "$searchstr" "$filesdir" | wc -l)

echo "The number of files are $file_count and the number of matching lines are $match_count"
