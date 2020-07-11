//===========================================================================
// common.h 
//===========================================================================
/*
Copyright (c) 2015 Princeton University
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of Princeton University nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY PRINCETON UNIVERSITY "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL PRINCETON UNIVERSITY BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef COMMON_H
#define COMMON_H

#include <inttypes.h>

#ifndef NOMPI
#include "mpi.h"
#endif

constexpr int MAX_THREADS_PER_PROCESS = 2048;

struct CtrlMsg
{
    enum Type {
        WRONG = 0,
        PROCESS_START = 1,
        PROCESS_FINISH = 2,
        THREAD_START = 3,
        THREAD_FINISH = 4
    };
    Type type = WRONG;
    int pid = 0;
    int tid = 0;
};

#pragma pack(push)  /* push current alignment to stack */
#pragma pack(1)     /* set alignment to 1 byte boundary */
struct InstMsg
{
    enum Type {
        WRONG = 0,
        THREAD_START = 1,
        THREAD_FINISH = 2,
        SYSCALL_IN = 3,
        SYSCALL_OUT = 4, 
        MEM_RD = 5,
        MEM_WR = 6,
        TERMINATE = 7
    };
    Type        type = WRONG;
    uint64_t    addr = 0; 
    uint32_t    ins_before = 0;
    bool        is_unlock = false;

    inline bool isWr() { return type == MEM_WR; };
};
#pragma pack(pop)   /* restore original alignment from stack */


#ifndef NOMPI
constexpr int server_tag = MPI_TAG_UB;
void createCommWithoutUncore(MPI_Comm& comm, MPI_Comm* barrier_comm);
int init_mpi(int* argc, char*** argv);
#endif


#endif  // COMMON_H
