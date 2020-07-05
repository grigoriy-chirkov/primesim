
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

MPICC := mpicc
MPICXX := mpicxx

OUT := $(PRIME_PATH)/bin/pin_wrapper
CXX_FILES := $(notdir $(wildcard *.cpp ../*.cpp))
O_FILES := $(CXX_FILES:%.cpp=$(PRIME_PATH)/obj/pin_wrapper/%.o)
OPT := -O3 -g

CXX_FLAGS := -std=c++17 -Wall -Werror $(OPT) -I.. \
             -DPIN=\"$(shell which pinbin)\" \
             -DPRIMELIB=\"$(PRIME_PATH)/bin/prime.so\"
LD_FLAGS :=  $(OPT)

.PHONY: clean

all: $(OUT)

$(PRIME_PATH)/obj/pin_wrapper/common.o: ../common.cpp 
	$(MPICXX) -c $< -o $@ $(CXX_FLAGS)

$(PRIME_PATH)/obj/pin_wrapper/%.o: %.cpp	
	$(MPICXX) -c $< -o $@ $(CXX_FLAGS)

$(OUT): $(O_FILES)
	$(MPICXX) $^ -o $@ $(LD_FLAGS)



clean:
	rm -f $(PRIME_PATH)/obj/pin_wrapper/*.o $(OUT)

