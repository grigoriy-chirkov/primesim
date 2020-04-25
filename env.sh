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

module load rh/devtoolset/7
module load intel/18.0

#export MPI_VER=2.0.2
#export MPI_COMPILER=intel170
#module load openmpi/intel-17.0/${MPI_VER}
module load intel-mpi/intel/2018.3

# Path to MPI lib, set to correct
#export MPI_LIB_PATH=\\\"/usr/local/openmpi/${MPI_VER}/${MPI_COMPILER}/x86_64/lib64/libmpi.so\\\"
export MPI_LIB_PATH=\\\"/opt/intel/compilers_and_libraries_2018.3.222/linux/mpi/intel64/lib/libmpi.so\\\"

# home directory for PriME, set it to the correct path
export PRIME_PATH=`pwd`

# home directory for Pin, set it to the correct path
export PINPATH=/tigress/gchirkov/pin-2.14-71313-gcc.4.4.7-linux

# set path
export PATH=$PRIME_PATH/tools:$PINPATH/intel64/bin:$PATH

#export OMPI_MCA_btl=tcp,sm,self
