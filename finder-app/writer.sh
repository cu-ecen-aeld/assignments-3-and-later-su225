#!/bin/bash

if [ "$#" -ne 2 ]; then
  echo "Usage: $0 <writefile> <writestr>"
  exit 1
fi

writefile="$1"
writestr="$2"

directory=$(dirname "$writefile")
if [ ! -d $directory ]; then
  mkdir -p $directory
fi

echo "$writestr" > "$writefile"
