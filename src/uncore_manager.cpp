//===========================================================================
// uncore_manager.cpp manages the uncore system
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
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <inttypes.h>
#include <cmath>
#include <assert.h>

#include "uncore_manager.h"
#include "common.h"

// Handle receiving MPI messages and send back responses
void *msgProducerWrapper(void *args)
{
    UncoreManager* uncore_manager = (UncoreManager*) args;
    uncore_manager->msgProducer();
    return NULL;
}

// Handle receiving MPI messages and send back responses
void *msgConsumerWrapper(void *args)
{
    HandlerArgs* targs = (HandlerArgs*) args;
    targs->uncore_manager->msgConsumer(targs->tid);
    return NULL;
}

void UncoreManager::init(const XmlSim* xml_sim)
{
    simulation_finished = false;
    sys = new System;
    assert(sys != NULL);
    sys->init(&xml_sim->sys);
    thread_sched = new ThreadSched;
    assert(thread_sched != NULL);
    thread_sched->init(getCoreCount());

    max_msg_size = xml_sim->max_msg_size;
    num_recv_threads = xml_sim->num_recv_threads;
    thread_sync_interval = xml_sim->thread_sync_interval;
    proc_sync_interval = xml_sim->proc_sync_interval;
    cpi_nonmem = xml_sim->sys.cpi_nonmem;
    freq = xml_sim->sys.freq;

    pthread_mutex_init(&mutex, NULL);

    handler_data = new HandlerData[num_recv_threads];
    assert(handler_data);
    for (int i = 0; i < num_recv_threads; i++) {
        handler_data[i].init(this, i);
    }

    core_data = new CoreData[getCoreCount()];
    assert(core_data);

    int rank, rc;
    rc = MPI_Comm_dup(MPI_COMM_WORLD, &comm);    
    if (rc != MPI_SUCCESS) {
        cerr << "Error duplicating world communicator. Terminating." << endl;
        MPI_Abort(MPI_COMM_WORLD, rc);
    }
    rc = MPI_Comm_rank(comm,&rank);
    if (rc != MPI_SUCCESS) {
        cerr << "Process not in new comm. Terminating." << endl;
        MPI_Abort(MPI_COMM_WORLD, rc);
    }    

    proc_list = new std::list<int>;
    // Match call to createCommWithoutUncore here with same call in core_manager, 
    // otherwise process will hang forever
    MPI_Comm   barrier_comm;
    createCommWithoutUncore(comm, &barrier_comm);

    barrier_cycle = thread_sync_interval;
    barrier_cnt = 0;
    num_threads_live = 0;
}

void UncoreManager::getSimStartTime()
{
    clock_gettime(CLOCK_REALTIME, &sim_start_time);
}

void UncoreManager::getSimFinishTime()
{
    clock_gettime(CLOCK_REALTIME, &sim_finish_time);
}


int UncoreManager::allocCore(int pid, int tid)
{
    return thread_sched->allocCore(pid, tid);
}


int UncoreManager::deallocCore(int pid, int tid)
{
    return thread_sched->deallocCore(pid, tid);
}


int UncoreManager::getCoreId(int pid, int tid)
{
    return thread_sched->getCoreId(pid, tid);
}

//Access the uncore system
int UncoreManager::uncore_access(int core_id, InsMem* ins_mem, int64_t timer)
{
    return sys->access(core_id, ins_mem, timer);
}

void UncoreManager::report(const char* result_basename)
{ 
    ofstream result_ofstream;
    result_ofstream.open(result_basename);

    uint64_t total_ins_count = 0;
    uint64_t total_nonmem_ins_count = 0;
    uint64_t total_mem_ins_count = 0;
    uint64_t total_cycles = 0;
    for (int i = 0; i < getCoreCount(); i++) {
        auto& cdata = core_data[i];
        if (!cdata.valid) continue;
        total_mem_ins_count += cdata.ins_mem;
        total_nonmem_ins_count += cdata.ins_nonmem;
        if (total_cycles < cdata.cycle) 
            total_cycles = cdata.cycle;
    }
    total_ins_count = total_mem_ins_count + total_nonmem_ins_count;

    result_ofstream << "*********************************************************\n";
    result_ofstream << "*                   PriME Simulator                     *\n";
    result_ofstream << "*********************************************************\n\n";
    double sim_time = (sim_finish_time.tv_sec - sim_start_time.tv_sec) + (double) (sim_finish_time.tv_nsec - sim_start_time.tv_nsec) / 1000000000.0; 
    result_ofstream << "Total computation time: " << sim_time <<" seconds" << endl;
    result_ofstream << "Total Execution time = " << total_cycles/(freq*pow(10.0,9)) <<" seconds\n";
    result_ofstream << "System frequency = "<< freq <<" GHz" << endl;
    result_ofstream << "Simulated slowdown : " << sim_time/(total_cycles/(freq*pow(10.0,9))) <<"X\n";
    result_ofstream << "Simulation speed: " << (total_ins_count/1000000.0)/sim_time << " MIPS" << endl; 
    result_ofstream << "Simulation runs " << total_ins_count <<" instructions, " << total_cycles <<" cycles\n";
    result_ofstream << "The average IPC = "<< double(total_ins_count) / double(total_cycles) << endl;
    result_ofstream << "Total memory instructions: "<< total_mem_ins_count <<endl;
    result_ofstream << "Total non-memory instructions: "<< total_nonmem_ins_count <<endl;
    

    //result_ofstream << "System call count : " << syscall_count <<endl;


    for(int i = 0; i < getCoreCount(); i++) {
        if (core_data[i].valid)
            core_data[i].report(result_ofstream);
    }
    result_ofstream << endl;
    
    //thread_sched->report(result_ofstream);
    sys->report(result_ofstream);

    result_ofstream.close();
}

void UncoreManager::lock() 
{
    pthread_mutex_lock(&mutex);
}

void UncoreManager::unlock() 
{
    pthread_mutex_unlock(&mutex);
}

void UncoreManager::add_proc(int pid) 
{
    proc_list->push_back(pid);
    proc_list->unique();      
}

void UncoreManager::rm_proc(int pid) 
{
    proc_list->remove(pid);
}

UncoreManager::~UncoreManager()
{
    pthread_mutex_destroy(&mutex);
    delete proc_list;
    delete [] handler_data;
    delete [] core_data;
    delete sys;
    delete thread_sched;
}     

void UncoreManager::msgProducer() {
    MPIMsg* inbuffer = new MPIMsg[max_msg_size + 1];
    assert(inbuffer != NULL);
    auto& msg = inbuffer[0];
    
    while(1) {
        MPI_Recv(inbuffer, (max_msg_size+1) * sizeof(MPIMsg), MPI_CHAR, MPI_ANY_SOURCE, 0, comm, MPI_STATUS_IGNORE);

        switch (msg.message_type) {
            case PROCESS_STARTING: { //Receive a msg indicating a new process
                lock();
                cout<<"[PriME] Pin process "<<msg.pid<<" begins"<<endl;
                add_proc(msg.pid);
                unlock();
                break;
            }
            case NEW_THREAD: {//Receive a msg indicating a new thread
                lock();
                int cid = allocCore(msg.pid, msg.tid);   
                if (cid == -1) {
                    cerr<< "Not enough cores for process "<<msg.pid<<" thread "<<msg.tid<<endl;
                    unlock();
                    MPI_Abort(MPI_COMM_WORLD, -1);
                } 
                core_data[cid].init(cid, msg.pid, msg.tid, getCycle(), max_msg_size);
                num_threads_live++;
                if (core_data[cid].cycle >= barrier_cycle) {
                    barrier_cnt++;
                }
                cout << "[PriME] Pin thread " << msg.pid << " from process " << msg.tid << " begins in core "<< cid << endl;
                unlock();
                break;
            }
            default: {
                lock();
                int cid = getCoreId(msg.pid, msg.tid);
                unlock();
                auto& cdata = core_data[cid];
                cdata.insert_msg(inbuffer, msg.payload_len);
                break;
            }
        }
    }
}

void UncoreManager::msgConsumer(int my_tid) {
    MPIMsg msg;
    memset(&msg, 0, sizeof(MPIMsg));

    while (1) {
        for (int cid = my_tid; cid < getCoreCount(); cid += num_recv_threads) {
            auto& cdata = core_data[cid];
            if (!cdata.valid)
                continue;

            if (cdata.cycle >= barrier_cycle) 
                continue;

            if (!cdata.eject_msg(&msg))
                continue;

            if (msg.is_control) {
                switch (msg.message_type) {
                    case PROCESS_FINISHING: {
                        lock();
                        rm_proc(msg.pid);
                        if (proc_list->empty()) {
                            simulation_finished = true;
                        }
                        cout<<"[PriME] Pin process "<<msg.pid<<" finishes"<<endl;
                        unlock();
                        break;
                    }
                    case THREAD_FINISHING: {
                        lock();
                        deallocCore(msg.pid, msg.tid);   
                        num_threads_live--;
                        cout << "[PriME] Thread " << msg.tid << " from process " << msg.pid << " finishes" <<endl;
                        unlock();
                        break;
                    }
                    case THREAD_LOCK:{
                        lock();
                        num_threads_live--;
                        cout << "[PriME] Thread " << msg.tid << " from process " << msg.pid << " locks" <<endl;
                        unlock();
                        break;
                    }
                    case THREAD_UNLOCK: {
                        lock();
                        num_threads_live++;
                        cdata.cycle = getCycle();
                        cout << "[PriME] Thread " << msg.tid << " from process " << msg.pid << " unlocks" <<endl;
                        unlock();
                        break;
                    }
                    case MEM_REQUESTS: {
                        lock();
                        cout<<"[PriME] Thread " << msg.tid << " from process " << msg.pid << " makes "<<msg.payload_len-1<<" memory requests "<<endl;
                        unlock();
                        break;
                    }
                    default:{
                        cerr<< "Wrong message type "<<msg.message_type<<endl;
                        MPI_Abort(MPI_COMM_WORLD, -1);
                    }
                }            
            } 
            else {
                InsMem ins_mem;
                memset(&ins_mem, 0, sizeof(ins_mem));

                ins_mem.pid = getProcId(cid);
                ins_mem.mem_type = msg.mem_type;
                ins_mem.addr_dmem = msg.addr_dmem;

                auto nonmem_inst = msg.ins_before; 
                auto nonmem_cycles = msg.ins_before * cpi_nonmem;
                cdata.nonmem_cycles += nonmem_cycles;
                cdata.cycle += nonmem_cycles;
                cdata.ins_nonmem += nonmem_inst;

                auto mem_cycles = uncore_access(cid, &ins_mem, cdata.cycle);
                cdata.cycle += mem_cycles;
                cdata.mem_cycles += mem_cycles;
                cdata.ins_mem++;
            }


            if (cdata.cycle >= barrier_cycle) {
                lock();
                barrier_cnt++;
                if (barrier_cnt >= num_threads_live) {
                    barrier_cnt = 0;
                    barrier_cycle += thread_sync_interval;
                }
                unlock();
            }
        }

        if (simulation_finished) {
            pthread_exit(NULL);
        }
    }
}

void UncoreManager::spawn_threads() 
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    getSimStartTime();
    for(int t = 0; t < num_recv_threads; t++) {
        cout << "[PriME] Main: creating uncore thread " << t <<endl;
        int rc = pthread_create(&handler_data[t].handle, &attr, msgConsumerWrapper, (void *)(&handler_data[t].args)); 
        
        if (rc) {
            cerr << "Error return code from pthread_create(): " << rc << endl;
            MPI_Finalize();
            exit(-1);
        }
    }

    int rc = pthread_create(&producer_thread_handle, &attr, msgProducerWrapper, (void *)(this)); 
    if (rc) {
        cerr << "Error return code from pthread_create(): " << rc << endl;
        MPI_Finalize();
        exit(-1);
    }    

    // Free attribute 
    pthread_attr_destroy(&attr);
}

void UncoreManager::collect_threads() 
{
    void *status;
    for(long t = 0; t < num_recv_threads; t++) { 
        int rc = pthread_join(handler_data[t].handle, &status);
        if (rc) {
            cerr << "Error return code from pthread_join(): " << rc << endl;
            MPI_Finalize();
            exit(-1);
        }
        cout << "[PriME] Main: completed join with uncore thread " << t << " having a status of "<< status << endl;
    }

    int rc = pthread_cancel(producer_thread_handle);
    if (rc) {
        cerr << "Error return code from pthread_join(): " << rc << endl;
        MPI_Finalize();
        exit(-1);
    }
    cout << "[PriME] Main: completed join with uncore thread producer" << endl;

    getSimFinishTime();
}

int UncoreManager::getCoreCount()
{
    return sys->getCoreCount();
}

uint64_t UncoreManager::getCycle()
{
    uint64_t cycle = 0;
    for (int i = 0; i < getCoreCount(); i++) {
        auto& cdata = core_data[i];
        if (!cdata.valid) continue;
        if (cycle < cdata.cycle) 
            cycle = cdata.cycle;
    }
    return cycle;
}

int UncoreManager::getProcId(int cid) 
{
    return thread_sched->getProcId(cid);
}
