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
    num_threads = xml_sim->num_recv_threads;
    assert(num_threads <= UNCORE_THREAD_MAX);

    pthread_mutex_init(&mutex, NULL);

    handler_data = new HandlerData[num_threads];
    assert(handler_data);
    for (int i = 0; i < num_threads; i++) {
        handler_data[i].init(this, i);
    }

    core_data = new CoreData[getCoreCount()];
    assert(core_data);
    for (int i = 0; i < getCoreCount(); i++) {
        core_data[i].init(max_msg_size);
    }

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
    string result_filename(result_basename);
    result_filename = result_filename + "_0";   
    ofstream result_ofstream;
    result_ofstream.open(result_filename.c_str());

    result_ofstream << "*********************************************************\n";
    result_ofstream << "*                   PriME Simulator                     *\n";
    result_ofstream << "*********************************************************\n\n";
    double sim_time = (sim_finish_time.tv_sec - sim_start_time.tv_sec) + (double) (sim_finish_time.tv_nsec - sim_start_time.tv_nsec) / 1000000000.0; 
    result_ofstream << "Total computation time: " << sim_time <<" seconds\n";
    result_ofstream<<endl;
    
    thread_sched->report(result_ofstream);
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

int UncoreManager::getNumThreads() {
    return num_threads;
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
        int src_pid = msg.pid;
        int src_tid = msg.tid;

        switch (msg.message_type) {
            case PROCESS_STARTING: {//Receive a msg indicating a new process
                lock();
                cout<<"[PriME] Pin process "<<src_pid<<" begins"<<endl;
                add_proc(src_pid);
                unlock();
                break;
            }
            case NEW_THREAD: {//Receive a msg indicating a new thread
                lock();
                int core_id = allocCore(src_pid, src_tid);   
                if (core_id == -1) {
                    cerr<< "Not enough cores for process "<<src_pid<<" thread "<<msg.mem_size<<endl;
                    unlock();
                    MPI_Abort(MPI_COMM_WORLD, -1);
                } 
                core_data[core_id].init(max_msg_size);
                unlock();
                break;
            }
            default: {
                lock();
                int core_id = getCoreId(src_pid, src_tid);
                unlock();
                auto& cdata = core_data[core_id];
                cdata.insert_msg(inbuffer, msg.payload_len);
                break;
            }
        }
    }
}

void UncoreManager::msgConsumer(int my_tid) {
    MPIMsg* inbuffer = new MPIMsg[max_msg_size + 1];
    assert(inbuffer != NULL);
    auto& msg = inbuffer[0];

    while (1) {
        for (int cid = 0; cid < getCoreCount(); cid++) {
            if (cid % getNumThreads() != my_tid)
                continue;

            auto& cdata = core_data[cid];
            if (!cdata.valid)
                continue;
            if (!cdata.eject_msg(inbuffer))
                continue;

            int src_pid = msg.pid;
            int src_tid = msg.tid;

            switch (msg.message_type) {
                case PROCESS_FINISHING: {//Receive a msg indicating a process has finished
                    lock();
                    cout<<"[PriME] Pin process "<<src_pid<<" finishes"<<endl;
                    rm_proc(src_pid);
                    if (proc_list->empty()) {
                        simulation_finished = true;
                    }
                    unlock();
                    break;
                }
                case THREAD_FINISHING: {//Receive a msg indicating a thread is finishing
                    lock();
                    deallocCore(src_pid, src_tid);   
                    unlock();
                    break;
                }
                case MEM_REQUESTS: {//Receive a msg of memory requests
                    int msg_len = msg.payload_len;
                    lock();
                    int core_id = getCoreId(src_pid, src_tid);
                    cout<<"[PriME] Pin thread " << src_tid << " from process "
                        <<src_pid<<" makes "<<msg_len-1<<" memory requests "<<endl;
                    unlock();
                    int delay = 0;
                    InsMem ins_mem;
                    memset(&ins_mem, 0, sizeof(ins_mem));

                    ins_mem.pid = src_pid;
                    for(int i = 1; i < msg_len; i++) {
                        ins_mem.mem_type = inbuffer[i].mem_type;
                        ins_mem.addr_dmem = inbuffer[i].addr_dmem;
                        delay += uncore_access(core_id, &ins_mem, inbuffer[i].timer + delay) - 1;
                        
                        if (delay < 0) {
                            cerr<<"Error: negative delay: "<<core_id<<" "<<ins_mem.pid<<" "<<src_tid<<" "
                                <<ins_mem.mem_type<<" "<<ins_mem.addr_dmem<<endl;
                            MPI_Abort(MPI_COMM_WORLD, -1);
                        }
                    }
                    MPI_Send(&delay, 1, MPI_INT, src_pid , src_tid, comm);
                    break;
                }
                default:{
                    cerr<< "Wrong message type "<<msg.message_type<<endl;
                    MPI_Abort(MPI_COMM_WORLD, -1);
                }
            }            
        }

        if (simulation_finished) {
            delete [] inbuffer;
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
    for(int t = 0; t < num_threads; t++) {
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
    for(long t = 0; t < num_threads; t++) { 
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

