#!/bin/sh
# compile generated translated-code files (gen/lift_*.c) into build/lift_*.o when out of date
cd "$(dirname "$0")"; mkdir -p build
for f in gen/lift_*.c; do
  o=build/$(basename "$f" .c).o
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ liftrt.h -nt "$o" ] || [ gen/lift_protos.h -nt "$o" ]; then gcc -O1 -w $LIFTFLAGS -c -I. "$f" -o "$o" & fi
done
wait
