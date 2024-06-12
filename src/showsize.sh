#!/bin/bash
nm -S ZIP_Router.linux --size-sort | grep " b " | awk '{ printf ("%40s %12i\n" ,$4, strtonum("0x"$2) ) }'
