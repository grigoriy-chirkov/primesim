
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

CC := icc
CXX := icpc

OUT := $(PRIME_PATH)/bin/prime.so 
CXX_FILES := $(notdir $(wildcard *.cpp ../*.cpp))
O_FILES := $(CXX_FILES:%.cpp=$(PRIME_PATH)/obj/pin/%.o)

CXX_FLAGS := -std=c++17 -Wall -Werror -O2 -g -I.. \
            -fomit-frame-pointer \
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
           -fPIC -DNOMPI

LD_FLAGS := -O2 -g -shared -Wl,--hash-style=sysv -Wl,-Bsymbolic \
           -Wl,--version-script=$(PIN_VERSION_SCRIPT)  \
           -Wl,--as-needed \
           -L$(PINPATH)/intel64/lib \
           -L$(PINPATH)/intel64/lib-ext  \
           -L$(PINPATH)/extras/xed-intel64/lib \
           -L$(PINPATH)/intel64/runtime/glibc \
           -fPIC -lpin -lxed -lpindwarf 
           

.PHONY: clean

all: $(OUT)

$(PRIME_PATH)/obj/pin/common.o: ../common.cpp 
	$(CXX) -c $< -o $@ $(CXX_FLAGS)

$(PRIME_PATH)/obj/pin/%.o: %.cpp 
	$(CXX) -c $< -o $@ $(CXX_FLAGS)

$(OUT): $(O_FILES)
	$(CXX) $^ -o $@ $(LD_FLAGS)


clean:
	rm -f $(O_FILES)  $(OUT) 

