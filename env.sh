#!/bin/bash

#Copyright (c) 2015 Princeton University
#All rights reserved.
#
#Redistribution and use in source and binary forms, with or without
#modification, are permitted provided that the following conditions are met:
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
#    * Neither the name of Princeton University nor the
#      names of its contributors may be used to endorse or promote products
#      derived from this software without specific prior written permission.
#
#THIS SOFTWARE IS PROVIDED BY PRINCETON UNIVERSITY "AS IS" AND
#ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
#WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
#DISCLAIMED. IN NO EVENT SHALL PRINCETON UNIVERSITY BE LIABLE FOR ANY
#DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
#(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
#LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
#ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
#SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# springdale 7 (from trin)
export MPI_VER=1.10.2
module load rh/devtoolset/7
module load openmpi/intel-17.0/${MPI_VER}
module load intel/17.0

export MPI_COMPILER=intel170
function mpiexec() { /usr/local/openmpi/${MPI_VER}/${MPI_COMPILER}/x86_64/bin/mpiexec "$@"; };
export -f mpiexec

export pin='pin -ifeellucky'

# home directory for PriME, set it to the correct path
export PRIME_PATH=`pwd`

# home directory for Pin, set it to the correct path
export PINPATH=/home/gchirkov/tank/pin-2.14-71313-gcc.4.4.7-linux

# Springdale 7
export OPENMPI_PATH=\"\\\"/usr/local/openmpi/${MPI_VER}/${MPI_COMPILER}/x86_64/lib64/libmpi.so\\\"\"
export OPENMPI_LIB_PATH=\"\\\"/usr/local/openmpi/${MPI_VER}/${MPI_COMPILER}/x86_64/lib64/libmpi.so\\\"\"

# path to libxml2 , set it to the correct path
export LIBXML2_PATH=/usr/include/libxml2

# path to PARSEC benchmarks, set it to the correct path if you want to run PARSEC
export PARSEC_PATH=/home/rpathak/ele585/parsec-3.0
#export PARSEC_PATH=/tigress/yfu/parsec-3.0

# set path
export PATH=$PRIME_PATH/tools:$PINPATH/:$PATH
