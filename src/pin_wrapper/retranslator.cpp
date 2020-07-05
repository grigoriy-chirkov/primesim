//===========================================================================
// retranslator.cpp 
//===========================================================================
/*
Copyright (c) 2020 Princeton University
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

#include "retranslator.h"
#include "mpi.h"
#include "common.h"
#include <cassert>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <iostream>


using namespace std;

Retranslator::Retranslator(int _pid, int _max_msg_size) : 
    pid(_pid), 
    max_msg_size(_max_msg_size)
{
    int rc = MPI_Comm_dup(MPI_COMM_WORLD, &comm);
    if (rc != MPI_SUCCESS) {
        cerr << "Couldn't create new communicator. Terminating." << endl;
        MPI_Abort(MPI_COMM_WORLD, rc);
    }

    // Create new new communicator without uncore process to 
    // barrier all core processes before simulation start
    MPI_Comm   barrier_comm;
    createCommWithoutUncore(comm, &barrier_comm);
    MPI_Barrier(barrier_comm);
    MPI_Comm_free(&barrier_comm);

}

Retranslator::~Retranslator() {
    MPI_Comm_free(&comm);
}

void Retranslator::thread_translator(int tid) {
    auto fifo_fd = open_pipe(tid);
    auto buf = make_unique<InstMsg[]>(max_msg_size);
    int cid = getCid(tid);
    while (1) {
        auto bytes_read = read(fifo_fd, buf.get(), sizeof(InstMsg)*max_msg_size);
        if (bytes_read == 0) break;
        retransmit(buf.get(), cid, bytes_read);
        if (buf[0].type == InstMsg::THREAD_FINISH) break;
    }
    close(fifo_fd);
}

void Retranslator::main_server() {
    auto fifo_fd = open_pipe();
    CtrlMsg buf;
    while (read(fifo_fd, &buf, sizeof(CtrlMsg)) != 0) {
        retransmit(&buf, server_tag, sizeof(CtrlMsg));
        if (buf.type == CtrlMsg::THREAD_START) {
            threads[buf.tid] = thread(&Retranslator::thread_translator, this, buf.tid);
        }
        if (buf.type == CtrlMsg::PROCESS_FINISH) {
            break;
        }
    }
    close(fifo_fd);
}

int Retranslator::open_pipe() {
    auto fifo_name = string("/scratch/prime_fifo_") + to_string(pid);
    int fifo_fd = -1;
    do {
        fifo_fd = open(fifo_name.c_str(), O_RDONLY);
    } while (fifo_fd < 0);
    return fifo_fd;
}

int Retranslator::open_pipe(int tid) {
    auto fifo_name = string("/scratch/prime_fifo_") + to_string(pid) + "_" + to_string(tid);
    int fifo_fd = -1;
    do {
        fifo_fd = open(fifo_name.c_str(), O_RDONLY);
    } while (fifo_fd < 0);
    return fifo_fd;
}

void Retranslator::retransmit(void* buf, int dst, int size) {
    MPI_Ssend(buf, size, MPI_CHAR, 0, dst, comm);
}

int Retranslator::getCid(int tid) {
    int cid = 0;
    MPI_Recv(&cid, 1, MPI_INT, 0, tid, comm, MPI_STATUS_IGNORE);
    return cid;
}

void Retranslator::collect_threads() {
    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();       
        }
    }
}