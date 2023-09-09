#!/bin/sh

if [ "$#" -ne 2 ]; then
  echo "Usage: $0 <filesdir> <searchstr>"
  exit 1
fi

filesdir="$1"
searchstr="$2"

if [ ! -d "$filesdir" ]; then
  echo "Error: '$filesdir' is not a valid directory"
  exit 1
fi

filecount=0
matchlines=0
for file in $(find "$filesdir" -type f); do
  filecount=$((filecount+1))
  curmatchline=$(grep -c "$searchstr" "$file")
  # if (($curmatchline > 0)); then
  #   matchlines=$((matchlines+$curmatchline))
  # fi
  if [ -n "$curmatchline" ] && [ "$curmatchline" -gt 0 ]; then
    matchlines=$((matchlines+curmatchline))
  fi
done

echo "The number of files are $filecount and the number of matching lines are $matchlines"
