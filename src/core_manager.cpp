//===========================================================================
// core_manager.cpp manages the cores
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


#include <iostream>
#include <limits>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <inttypes.h>
#include <cmath>
#include <assert.h>

#include "common.h"
#include "core_manager.h"

using namespace std;


void CoreManager::init(XmlSim* xml_sim, MPI_Comm _comm)
{
    int rc;
    num_threads_online = 0;
    max_threads = 0;
    barrier_counter = 0;
    barrier_time = 0;
    syscall_count = 0;
    sync_syscall_count = 0;

    cpi_nonmem = xml_sim->sys.cpi_nonmem;
    max_msg_size = xml_sim->max_msg_size;
    thread_sync_interval = xml_sim->thread_sync_interval;
    proc_sync_interval = xml_sim->proc_sync_interval;
    syscall_cost = xml_sim->syscall_cost;
    freq = xml_sim->sys.freq;
    num_recv_threads = xml_sim->num_recv_threads;
    comm = _comm;

    rc = MPI_Comm_size(comm,&num_procs);
    if (rc != MPI_SUCCESS) {
        cerr << "Process not in the communicator. Terminating.\n";
        MPI_Abort(MPI_COMM_WORLD, rc);
    }
    rc = MPI_Comm_rank(comm,&rank);
    if (rc != MPI_SUCCESS) {
        cerr << "Process not in the communicator. Terminating.\n";
        MPI_Abort(MPI_COMM_WORLD, rc);
    }

    //# of core processes equals num_tasks-1 because there is a uncore process
    num_procs--;

    for(int i = 0; i < THREAD_MAX; i++) {
        thread_data[i].init(max_msg_size);
    }

    PIN_MutexInit(&mutex);
    PIN_SemaphoreInit(&sem);
    PIN_SemaphoreClear(&sem);
}

void CoreManager::startSim()
{
    int rc;
    auto& tdata = thread_data[0];
    auto& msg = tdata.msgs[0];
    msg.message_type = PROCESS_STARTING;
    msg.thread_id = 0;
    msg.payload_len = 1;
    MPI_Send(&msg, sizeof(MPIMsg), MPI_CHAR, 0, 0, comm);

    // Create new new communicator without uncore process to 
    // barrier all core processes before simulation start
    int barrier_rank;
    MPI_Comm   barrier_comm;
    createCommWithoutUncore(comm, &barrier_comm);

    rc = MPI_Comm_rank(barrier_comm, &barrier_rank);
    if (rc != MPI_SUCCESS) {
        cerr << "Process not in the barrier comm. Terminating.\n";
        MPI_Abort(MPI_COMM_WORLD, rc);
    }


    MPI_Barrier(barrier_comm);
    barrier_time = thread_sync_interval;
}


// This function is called before every instruction is executed
void CoreManager::insCount(uint32_t ins_count_in, THREADID threadid)
{
    thread_data[threadid].ins_count += ins_count_in;
}


bool CoreManager::isOtherThreadWaiting(THREADID threadid) 
{
    for(uint32_t i = 0; i< max_threads; i++) {
        if(i != threadid && thread_data[i].thread_state == WAIT) {
            return true;
        }    
    }
    return false;
}

// This function implements periodic barriers across all threads within one process and all processes 
void CoreManager::barrier(THREADID threadid)
{
    auto& tdata = thread_data[threadid];
    
    if ( (num_threads_online < 2) || 
         (tdata.thread_state != ACTIVE) || 
         (tdata.cycle < barrier_time)      )
        return;

    //Send out all remaining memory requests in the buffer
    if (tdata.mpi_pos > 1) {
        drainMemReqs(threadid);
    }

    PIN_MutexLock(&mutex);

    tdata.thread_state = WAIT;
    
    if (barrier_counter == 0) {
        // Wait until all threads from previous barrier leave it
        while(isOtherThreadWaiting(threadid));
        PIN_SemaphoreClear(&sem);
    }
    
    
    barrier_counter++;


    if (barrier_counter == num_threads_online) {
        barrier_time += thread_sync_interval;
        barrier_counter = 0;
        PIN_SemaphoreSet(&sem);
    }

    PIN_MutexUnlock(&mutex);



    while (!PIN_SemaphoreTimedWait(&sem, 1LU)) {
        //Leave barrier once the Semaphore is set
        if (!PIN_MutexTryLock(&mutex)) {
            continue;
        }
        
        if (barrier_counter == num_threads_online) {
            barrier_time += thread_sync_interval;
            barrier_counter = 0;
            PIN_SemaphoreSet(&sem);
            PIN_MutexUnlock(&mutex);
            break;
        }
        else {
            PIN_MutexUnlock(&mutex);
            PIN_Yield();
        }
    }
    
    tdata.thread_state = ACTIVE;
} 


// This function returns the average timer accoss all cores
double CoreManager::getAvgCycle()
{
    double avg_cycle = 0, avg_count = 0;
    for (int i = 0; i < max_threads; i++) {
        auto& tdata = thread_data[i];
        if (tdata.thread_state == ACTIVE) {
            avg_cycle += tdata.cycle;
            avg_count ++;
        }
    }
    if (avg_count == 0) {
        for (int i = 0; i < max_threads; i++) {
            auto& tdata = thread_data[i];
            if (tdata.cycle > avg_cycle) {
                avg_cycle = tdata.cycle;
            }
        }
    }
    else {
        avg_cycle = avg_cycle / avg_count;
    }
    return avg_cycle;
}



// Handle non-memory instructions
void CoreManager::execNonMem(uint32_t ins_count_in, THREADID threadid)
{
    auto& tdata = thread_data[threadid];
    tdata.cycle += cpi_nonmem * ins_count_in;
    tdata.ins_nonmem += ins_count_in;
    barrier(threadid); 
}




// Handle a memory instruction
void CoreManager::execMem(void * addr, THREADID threadid, uint32_t size, BOOL mem_type)
{
    ThreadData& tdata = thread_data[threadid];
    MPIMsg& msg = tdata.msgs[tdata.mpi_pos];
    msg.mem_type = mem_type;
    msg.addr_dmem = (uint64_t) addr;
    msg.mem_size = size;
    msg.timer = (int64_t)(tdata.cycle);
    
    tdata.cycle += 1;
    tdata.mpi_pos++;
    if (tdata.mpi_pos >= (max_msg_size + 1)) {
        drainMemReqs(threadid);
    }
    barrier(threadid);
}

void CoreManager::drainMemReqs(THREADID threadid) {
    ThreadData& tdata = thread_data[threadid];
    tdata.delay = 0;
    
    MPIMsg& msg = tdata.msgs[0];
    msg.message_type = MEM_REQUESTS;
    msg.thread_id = threadid;
    msg.payload_len = tdata.mpi_pos;

    MPI_Send(tdata.msgs, tdata.mpi_pos * sizeof(MPIMsg), MPI_CHAR, 0, tdata.uncore_thread, comm);
    MPI_Recv(&tdata.delay, 1, MPI_INT, 0, threadid, comm, MPI_STATUS_IGNORE);
    
    if(tdata.delay == -1) {
        cerr<<"An error occurs in cache system\n";
        MPI_Abort(MPI_COMM_WORLD, -1);
    }

    tdata.cycle += tdata.delay;
    tdata.mpi_pos = 1;
}

// This routine is executed every time a thread starts.
void CoreManager::threadStart(THREADID threadid, CONTEXT *ctxt, int32_t flags, void *v)
{
    PIN_MutexLock(&mutex);
    ThreadData& tdata = thread_data[threadid];


    if( threadid >= THREAD_MAX ) {
        cerr << "Error: the number of threads exceeds the limit!\n";
    }
    int i;
    for(i = 0; i < max_threads; i++) {
        auto& tdata_i = thread_data[i];
        if(PIN_GetParentTid() == tdata_i.core_thread) {
            tdata.cycle = tdata_i.cycle;
            break;
        }
    }
    if(i == max_threads) {
        tdata.cycle = thread_data[0].cycle;
    }
    
    num_threads_online++; 
    max_threads++; 
    tdata.thread_state = ACTIVE;
    tdata.core_thread = PIN_GetTid();
    cout << "[PriME] Pin thread " << threadid << " from process " << getRank() << " begins at cycle "<< (uint64_t)tdata.cycle<< endl;

    PIN_MutexUnlock(&mutex);

    MPIMsg& msg = tdata.msgs[0];
    msg.message_type = NEW_THREAD;
    msg.thread_id = threadid;
    msg.payload_len = 1;

    MPI_Send(&msg, sizeof(MPIMsg), MPI_CHAR, 0, tdata.uncore_thread, comm);
    MPI_Recv(&tdata.uncore_thread, 1, MPI_INT, 0, threadid, comm, MPI_STATUS_IGNORE);
}


// This routine is executed every time a thread is destroyed.
void CoreManager::threadFini(THREADID threadid, const CONTEXT *ctxt, int32_t code, void *v)
{
    auto& tdata = thread_data[threadid];
    //Send out all remaining memory requests in the buffer
    if(tdata.mpi_pos > 1) {
        drainMemReqs(threadid);
    }

    PIN_MutexLock(&mutex);

    num_threads_online--;
    tdata.thread_state = FINISH;
    cout << "[PriME] Thread " << threadid << " from process " << getRank() << " finishes at cycle "<< (uint64_t)tdata.cycle <<endl;
    PIN_MutexUnlock(&mutex);


    MPIMsg& msg = tdata.msgs[0];
    msg.message_type = THREAD_FINISHING;
    msg.thread_id = threadid;
    msg.payload_len = 1;

    MPI_Send(&msg, sizeof(MPIMsg), MPI_CHAR, 0, tdata.uncore_thread, comm);
}


// Called before syscalls
void CoreManager::sysBefore(ADDRINT ip, ADDRINT num, ADDRINT arg0, ADDRINT arg1, ADDRINT arg2, 
               ADDRINT arg3, ADDRINT arg4, ADDRINT arg5, THREADID threadid)
{
    if((num == SYS_futex &&  ((arg1&FUTEX_CMD_MASK) == FUTEX_WAIT || (arg1&FUTEX_CMD_MASK) == FUTEX_LOCK_PI))
    ||  num == SYS_wait4
    ||  num == SYS_waitid) {
        PIN_MutexLock(&mutex);

        auto& tdata = thread_data[threadid];

        if(tdata.thread_state == ACTIVE) {
            tdata.thread_state = SUSPEND;
            num_threads_online--;
            sync_syscall_count++;
        }
        PIN_MutexUnlock(&mutex);
    }
    syscall_count++;
}

// Called after syscalls
void CoreManager::sysAfter(ADDRINT ret, THREADID threadid)
{
    PIN_MutexLock(&mutex);

    auto& tdata = thread_data[threadid];
    if (tdata.thread_state == SUSPEND) {
        tdata.cycle = getAvgCycle();
        tdata.thread_state = ACTIVE;
        num_threads_online++;
    }
    else {
        tdata.cycle += syscall_cost;
    }
    PIN_MutexUnlock(&mutex);

}



// Enter a syscall
void CoreManager::syscallEntry(THREADID threadIndex, CONTEXT *ctxt, SYSCALL_STANDARD std, void *v)
{
    sysBefore(PIN_GetContextReg(ctxt, REG_INST_PTR),
        PIN_GetSyscallNumber(ctxt, std),
        PIN_GetSyscallArgument(ctxt, std, 0),
        PIN_GetSyscallArgument(ctxt, std, 1),
        PIN_GetSyscallArgument(ctxt, std, 2),
        PIN_GetSyscallArgument(ctxt, std, 3),
        PIN_GetSyscallArgument(ctxt, std, 4),
        PIN_GetSyscallArgument(ctxt, std, 5),
        threadIndex);
}

// Exit a syscall
void CoreManager::syscallExit(THREADID threadIndex, CONTEXT *ctxt, SYSCALL_STANDARD std, void *v)
{
    sysAfter(PIN_GetSyscallReturn(ctxt, std), threadIndex);
}

void CoreManager::getSimStartTime()
{
    clock_gettime(CLOCK_REALTIME, &sim_start_time);
}

void CoreManager::getSimFinishTime()
{
    clock_gettime(CLOCK_REALTIME, &sim_finish_time);
}



void CoreManager::report(ofstream& result)
{
    double total_cycles = 0, total_ins_counts = 0,  total_nonmem_ins_counts = 0;
    double total_cycles_nonmem = 0;
    total_cycles = (thread_data[0].cycle);
    int i;
    result << fixed << std::setprecision(1);
    for (i = 0; i < max_threads; i++) {
    //    total_cycles += cycle[i]._count;
        auto& tdata = thread_data[i];
        total_ins_counts += tdata.ins_count;
        total_nonmem_ins_counts += tdata.ins_nonmem;
        cout << "[PriME] Thread " <<i<< " from process " << getRank() << " runs " << tdata.ins_count <<" instructions\n";
    }
    total_cycles_nonmem = total_nonmem_ins_counts * cpi_nonmem;
    double sim_time = (sim_finish_time.tv_sec - sim_start_time.tv_sec) + (double) (sim_finish_time.tv_nsec - sim_start_time.tv_nsec) / 1000000000.0; 
    result << "*********************************************************\n";
    result << "*                   PriME Simulator                     *\n";
    result << "*********************************************************\n\n";
    result << "Application "<<rank<<endl<<endl;
    result << "Elapsed time "<< sim_time <<" seconds\n";
    result << "Simulation speed: " << (total_ins_counts/1000000.0)/sim_time << " MIPS\n"; 
    result << "Simulated slowdown : " << sim_time/(total_cycles/(freq*pow(10.0,9))) <<"X\n";
    result << "Total Execution time = " << total_cycles/(freq*pow(10.0,9)) <<" seconds\n";
    result << "System frequence = "<< freq <<" GHz\n";
    result << "Simulation runs " << total_cycles <<" cycles\n";
    for(i = 0; i < max_threads; i++) {
        auto& tdata = thread_data[i];
        result << "Thread " <<i<< " runs " << tdata.cycle <<" cycles\n";
        result << "Thread " <<i<< " runs " << tdata.ins_count <<" instructions\n";
    }
    result << "Simulation runs " << total_ins_counts <<" instructions\n\n";
    result << "Simulation result:\n\n";
    result << "The average IPC = "<< total_ins_counts / total_cycles << "\n";
    result << "Total memory instructions: "<< total_ins_counts - total_nonmem_ins_counts <<endl;
    result << "Total memory access cycles: "<< (total_cycles - total_cycles_nonmem) <<endl;
    result << "Total non-memory instructions: "<< total_nonmem_ins_counts <<endl;
    result << "Total non-memory access cycles: "<< (total_cycles_nonmem) <<endl;
    result << "System call count : " << syscall_count <<endl;
    result << "Sync System call count : " << sync_syscall_count <<endl;
    result << scientific;
}

void CoreManager::finishSim(int32_t code, void *v)
{

    uint64_t total_ins_counts = 0;
    for (int i = 0; i < max_threads; i++) {
        total_ins_counts += thread_data[i].ins_count;
    }
    double sim_time = (sim_finish_time.tv_sec - sim_start_time.tv_sec) + (double) (sim_finish_time.tv_nsec - sim_start_time.tv_nsec) / 1000000000.0; 
    cout << "*********************************************************\n";
    cout << "Application "<<rank<<" completes\n\n";
    cout << "Elapsed time : "<< sim_time <<" seconds\n";
    cout << "Simulation speed : " << (total_ins_counts/1000000.0)/sim_time << " MIPS\n"; 
    cout << "Simulated time : " << (uint64_t)(thread_data[0].cycle)/(freq*pow(10.0,9)) <<" seconds\n";
    cout << "Simulated slowdown : " << sim_time/(thread_data[0].cycle/(freq*pow(10.0,9))) <<"X\n";
    cout << "*********************************************************\n";
    PIN_MutexFini(&mutex);
    PIN_SemaphoreFini(&sem);

    auto& tdata = thread_data[0];
    auto& msg = tdata.msgs[0];
    msg.message_type = PROCESS_FINISHING;
    msg.payload_len = 0;
    msg.thread_id = 0;

    MPI_Send(&msg, sizeof(MPIMsg), MPI_CHAR, 0, 0, comm);
    MPI_Recv(&num_procs, 1, MPI_INT, 0, 0, comm, MPI_STATUS_IGNORE);
    if (num_procs == 0) {
        for (int i = 0; i < num_recv_threads; i++) {
            msg.message_type = PROGRAM_EXITING;
            MPI_Send(&msg, sizeof(MPIMsg), MPI_CHAR, 0, i, comm);
        }
    }
    MPI_Finalize();
}

int CoreManager::getRank() {
    return rank;
}

CoreManager::~CoreManager()
{

}       
