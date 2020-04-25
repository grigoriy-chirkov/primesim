
###########################################################################
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
##############################################################################


PIN_VERSION_SCRIPT = $(shell find $(PINPATH) -name pintool.ver)
GRAPHITE_PATH = $(PRIME_PATH)/src/Graphite

CC := icc
CXX := icpc
MPICC := mpicc
MPICXX := mpicxx

TOP_LEVEL_PROGRAM_NAME := bin/prime.so bin/prime 
CXX_FILES := $(wildcard src/*.cpp src/Graphite/*.cpp)
DEP_FILES := $(CXX_FILES:src/%.cpp=dep/%.d)
O_FILES := $(filter-out obj/core_manager.o, $(filter-out obj/pin_prime.o, $(CXX_FILES:src/%.cpp=obj/%.o)))
PIN_O_FILES := obj/pin_prime.o obj/pin_common.o obj/core_manager.o

CXX_FLAGS := -std=c++14 -Wall -Werror -Wno-unknown-pragmas -O0 $(shell xml2-config --cflags) -I$(GRAPHITE_PATH)
LD_FLAGS :=  -O0 $(shell xml2-config --libs) -ldl -lrt -lpthread -mt_mpi

PIN_CXX_FLAGS := $(CXX_FLAGS) -fomit-frame-pointer \
           -fno-stack-protector -fno-strict-aliasing \
           -D_GLIBCXX_USE_CXX11_ABI=0 -fabi-version=2 \
           -DBIGARRAY_MULTIPLIER=1 -DUSING_XED  \
           -DFUND_TC_TARGETCPU=FUND_CPU_INTEL64 \
           -DFUND_TC_HOSTCPU=FUND_CPU_INTEL64 \
           -DFUND_TC_TARGETOS=FUND_OS_LINUX \
           -DFUND_TC_HOSTOS=FUND_OS_LINUX \
           -DTARGET_IA32E -DHOST_IA32E \
           -DTARGET_LINUX \
           -I$(PINPATH)/source/tools/InstLib \
           -I$(PINPATH)/extras/xed-intel64/include \
           -I$(PINPATH)/extras/components/include \
           -I$(PINPATH)/source/include/pin \
           -I$(PINPATH)/source/include/pin/gen \
           -fPIC 

PIN_LD_FLAGS := -shared -Wl,--hash-style=sysv -Wl,-Bsymbolic \
           -Wl,--version-script=$(PIN_VERSION_SCRIPT)  \
           -Wl,--as-needed \
           -L$(PINPATH)/intel64/lib \
           -L$(PINPATH)/intel64/lib-ext  \
           -L$(PINPATH)/extras/xed-intel64/lib \
           -L$(PINPATH)/intel64/runtime/glibc \
           -fPIC \
           $(LD_FLAGS) -lpin -lxed -lpindwarf 
           

.PHONY: clean

all: $(TOP_LEVEL_PROGRAM_NAME)


obj/pin_prime.o: src/pin_prime.cpp dep/pin_prime.d
	$(MPICXX) -c $< -o $@ $(PIN_CXX_FLAGS) -DMPI_PATH=$(MPI_LIB_PATH)

obj/pin_common.o: src/common.cpp dep/common.d
	$(MPICXX) -c $< -o $@ $(PIN_CXX_FLAGS)

obj/core_manager.o: src/core_manager.cpp dep/core_manager.d
	$(MPICXX) -c $< -o $@ $(PIN_CXX_FLAGS)

obj/Graphite/%.o: src/Graphite/%.cpp dep/Graphite/%.d 
	$(MPICXX) -c $< -o $@ $(CXX_FLAGS)

obj/%.o: src/%.cpp dep/%.d 
	$(MPICXX) -c $< -o $@ $(CXX_FLAGS)


dep/pin_prime.d: src/pin_prime.cpp
	$(MPICXX) -MM $(PIN_CXX_FLAGS) -MT '$(patsubst src/%.cpp,obj/%.o,$<)' $< -MF $@

dep/core_manager.d: src/core_manager.cpp
	$(MPICXX) -MM $(PIN_CXX_FLAGS) -MT '$(patsubst src/%.cpp,obj/%.o,$<)' $< -MF $@

dep/Graphite/%.d: src/Graphite/%.cpp
	$(MPICXX) -MM $(CXX_FLAGS) -MT '$(patsubst src/Graphite/%.cpp,obj/Graphite/%.o,$<)' $< -MF $@

dep/%.d: src/%.cpp
	$(MPICXX) -MM $(CXX_FLAGS) -MT '$(patsubst src/%.cpp,obj/%.o,$<)' $< -MF $@


bin/prime: $(O_FILES)
	$(MPICXX) $^ -o $@ $(LD_FLAGS)

bin/prime.so: $(PIN_O_FILES)
	$(MPICXX) $^ -o $@ $(PIN_LD_FLAGS)


clean:
	rm -f dep/*.d dep/Graphite/*.d obj/*.o obj/Graphite/*.o bin/* 

