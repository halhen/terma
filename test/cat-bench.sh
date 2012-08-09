#!/bin/bash

URL=http://www.ietf.org/rfc/rfc3261.txt
FILENAME="$(basename "$URL")"

ITERATIONS=100


[[ -f  "$FILENAME" ]] || wget "$URL"

time (for i in {1..100}; do cat "$FILENAME"; done)
