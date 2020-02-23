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




enum MessageTypes
{
    MEM_REQUESTS = 0,
    PROCESS_STARTING = -3,
    PROCESS_FINISHING = -1,
    INTER_PROCESS_BARRIERS = -2,
    NEW_THREAD = -4,
    THREAD_FINISHING = -8,
    PROGRAM_EXITING = -5
};

struct MPIMsg
{
    union {
        struct {
            uint64_t     message_type;
            uint64_t     thread_id;
            uint64_t     payload_len;
        };
        struct {
            bool        mem_type; //1 means write, 0 means read
            int         mem_size; 
            uint64_t    addr_dmem; 
            int64_t     timer;

        };
    };
};

enum MemType
{
    RD    = 0,  //read
    WR    = 1,  //write
    WB    = 2   //writeback
};

enum ThreadState
{
    DEAD    = 0,
    ACTIVE  = 1,
    SUSPEND = 2,
    FINISH  = 3,
    WAIT    = 4
};

static void createCommWithoutUncore(MPI_Comm comm, MPI_Comm* barrier_comm) {
    // Create new new communicator without uncore process to 
    // barrier all core processes before simulation start
    int rc;
    int rank_excl[1] = {0};
    MPI_Group prime_group, barrier_group;
    rc = MPI_Comm_group(comm, &prime_group); 
    if (rc != MPI_SUCCESS) {
        std::cerr << "Could not extract group. Terminating.\n";
        MPI_Abort(MPI_COMM_WORLD, rc);
    }

    rc = MPI_Group_excl(prime_group, 1, rank_excl, &barrier_group);
    if (rc != MPI_SUCCESS) {
        std::cerr << "Could not create barrier group. Terminating.\n";
        MPI_Abort(MPI_COMM_WORLD, rc);
    }

    rc = MPI_Comm_create(comm, barrier_group, barrier_comm);
    if (rc != MPI_SUCCESS) {
        std::cerr << "Could not create barrier comm. Terminating.\n";
        MPI_Abort(MPI_COMM_WORLD, rc);
    }
}



#endif  // COMMON_H
