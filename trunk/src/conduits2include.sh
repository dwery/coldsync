#!/bin/sh
ls conduits/*.h | awk '{ print "#include \"" $1 "\"" }'
