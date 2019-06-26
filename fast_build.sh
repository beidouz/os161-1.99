#!/bin/bash
set -eu
topdir=`pwd`
buildir='~/cs350-os161/os161-1.99'
eval cd $buildir/kern/compile/ASST$1
bmake depend
bmake
bmake install
echo "SUCCESS!!!!!!!!!!"
echo ""
cd $topdir
