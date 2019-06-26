#!/bin/bash
set -eu
topdir=`pwd`
buildir='~/cs350-os161/os161-1.99'
eval cd $buildir
cd ./kern/compile/ASST$1
bmake depend
bmake
bmake install
eval cd $buildir
bmake
bmake install
cd $topdir