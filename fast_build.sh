#!/bin/bash
set -eu
topdir=$(pwd)
cd ./kern/compile/ASST$1
bmake depend
bmake
bmake install
echo "SUCCESS!!!!!!!!!!"
echo ""
