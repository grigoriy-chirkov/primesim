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

#include "mpi.h"

enum MessageType
{
    MEM_REQUESTS = 0,
    PROCESS_STARTING = 3,
    PROCESS_FINISHING = 1,
    NEW_THREAD = 4,
    THREAD_FINISHING = 8,
    PROGRAM_EXITING = 5,
    THREAD_LOCK = 6,
    THREAD_UNLOCK = 7, 
    TERMINATE = 2
};

struct MPIMsg
{   
    bool is_control; 
    union {
        struct { // control msg
            MessageType  message_type;
            int          tid;
            int          pid;
            int          payload_len;
        } __attribute__((packed));
        struct { // mem msg
            bool        mem_type; //1 means write, 0 means read
            uint64_t    addr_dmem; 
            uint32_t    ins_before;
        } __attribute__((packed));
    } __attribute__((packed));
} __attribute__((packed));


enum ThreadState
{
    DEAD    = 0,
    ACTIVE  = 1,
    LOCKED  = 2,
    FINISH  = 3,
    WAIT    = 4
};

void createCommWithoutUncore(MPI_Comm& comm, MPI_Comm* barrier_comm);



#endif  // COMMON_H
