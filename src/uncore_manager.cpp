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
void *msgHandlerWrapper(void *args)
{
    UncoreThreadArgs* targs = (UncoreThreadArgs*) args;
    targs->uncore_manager->msgHandler(targs->tid);
    return NULL;
}

void UncoreManager::init(const XmlSim* xml_sim, const char* result_basename)
{
    sys.init(&xml_sim->sys);
    thread_sched.init(sys.getCoreCount());
    max_msg_size = xml_sim->max_msg_size;
    num_threads = xml_sim->num_recv_threads;
    assert(num_threads <= THREAD_MAX);

    pthread_mutex_init(&mutex, NULL);
    for (int i = 0; i < num_threads; i++) {
        thread_data[i].init(this, max_msg_size, i);
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

    stringstream result_filename;
    result_filename << result_basename << "_" << rank;
    result_ofstream.open(result_filename.str().c_str());

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


int UncoreManager::allocCore(int prog_id, int thread_id)
{
    return thread_sched.allocCore(prog_id, thread_id);
}


int UncoreManager::deallocCore(int prog_id, int thread_id)
{
    return thread_sched.deallocCore(prog_id, thread_id);
}


int UncoreManager::getCoreId(int prog_id, int thread_id)
{
    return thread_sched.getCoreId(prog_id, thread_id);
}

//Access the uncore system
int UncoreManager::uncore_access(int core_id, InsMem* ins_mem, int64_t timer)
{
    return sys.access(core_id, ins_mem, timer);
}

void UncoreManager::report()
{
    result_ofstream << "*********************************************************\n";
    result_ofstream << "*                   PriME Simulator                     *\n";
    result_ofstream << "*********************************************************\n\n";
    double sim_time = (sim_finish_time.tv_sec - sim_start_time.tv_sec) + (double) (sim_finish_time.tv_nsec - sim_start_time.tv_nsec) / 1000000000.0; 
    result_ofstream << "Total computation time: " << sim_time <<" seconds\n";
    result_ofstream<<endl;
    
    thread_sched.report(result_ofstream);
    sys.report(result_ofstream);
}

void UncoreManager::lock() 
{
    pthread_mutex_lock(&mutex);
}

void UncoreManager::unlock() 
{
    pthread_mutex_unlock(&mutex);
}

void UncoreManager::add_proc(int proc_id) 
{
    proc_list.push_back(proc_id);
    proc_list.unique();      
}

void UncoreManager::rm_proc(int proc_id) 
{
    proc_list.remove(proc_id);
}

int UncoreManager::get_num_threads() {
    return num_threads;
}

int UncoreManager::get_max_msg_size() {
    return max_msg_size;
}

UncoreManager::~UncoreManager()
{
    result_ofstream.close();
    pthread_mutex_destroy(&mutex);
}     


void UncoreManager::msgHandler(int my_tid)
{
    while (1) {
        MPI_Status local_status;
        UncoreThreadData& tdata = thread_data[my_tid];
        MPIMsg& msg = tdata.msgs[0];
        MPI_Recv(tdata.msgs, (max_msg_size + 1)*sizeof(MPIMsg), MPI_CHAR, MPI_ANY_SOURCE, my_tid , comm, &local_status);
        int src_proc_id = local_status.MPI_SOURCE;
        lock();

        switch (msg.message_type) {
            case PROCESS_STARTING: {//Receive a msg indicating a new process
                cout<<"[PriME] Pin process "<<src_proc_id<<" begins"<<endl;
                add_proc(src_proc_id);
                unlock();
                break;
            }
            case PROCESS_FINISHING: {//Receive a msg indicating a process has finished
                cout<<"[PriME] Pin process "<<src_proc_id<<" finishes"<<endl;
                rm_proc(src_proc_id);
                int proc_num = proc_list.size();
                MPI_Send(&proc_num, 1, MPI_INT, src_proc_id , 0, comm);
                
                if (barrier_proc_count >= proc_num) {                    
                    for(auto proc_id : proc_list) {
                        MPI_Send(&proc_num, 1, MPI_INT, proc_id, 0, comm);
                    }
                    barrier_proc_count = 0; 
                }
                unlock();
                break;
            }
            case INTER_PROCESS_BARRIERS: {//Receive a msg for inter-process barriers
                cout<<"[PriME] Pin process "<<src_proc_id<<" meets a barrier"<<endl;
                barrier_proc_count++;
                if (barrier_proc_count >= proc_list.size()) {
                    int proc_num = proc_list.size();
                    for(auto proc_id : proc_list) {
                        MPI_Send(&proc_num, 1, MPI_INT, proc_id, 0, comm);
                    }
                    barrier_proc_count = 0; 
                } 
                unlock();
                break;
            }
            case NEW_THREAD: {//Receive a msg indicating a new thread
                int core_id = allocCore(src_proc_id, msg.thread_id);   
                if (core_id == -1) {
                    cerr<< "Not enough cores for process "<<src_proc_id<<" thread "<<msg.mem_size<<endl;
                    report();
                    unlock();
                    MPI_Abort(MPI_COMM_WORLD, -1);
                } 
                else {
                    int uncore_thread = core_id % get_num_threads();
                    MPI_Send(&uncore_thread, 1, MPI_INT, src_proc_id, msg.thread_id, comm);
                    unlock();
                }
                break;
            }
            case THREAD_FINISHING: {//Receive a msg indicating a thread is finishing
                deallocCore(src_proc_id, msg.thread_id);   
                unlock();
                break;
            }
            case PROGRAM_EXITING: { //Receive a msg to terminate the execution
                unlock();
                pthread_exit(NULL);
                break;
            }
            case MEM_REQUESTS: {//Receive a msg of memory requests
                int thread_id = msg.thread_id;
                int msg_len = msg.payload_len;
                int core_id = getCoreId(src_proc_id, thread_id);
                cout<<"[PriME] Pin thread " << thread_id << " from process "
                    <<src_proc_id<<" makes "<<msg_len-1<<" memory requests "<<endl;
                unlock();
                int delay = 0;
                InsMem ins_mem;
                memset(&ins_mem, 0, sizeof(ins_mem));

                
                ins_mem.prog_id = src_proc_id;
                for(int i = 1; i < msg_len; i++) {
                    ins_mem.mem_type = tdata.msgs[i].mem_type;
                    ins_mem.addr_dmem = tdata.msgs[i].addr_dmem;
                    delay += uncore_access(core_id, &ins_mem, tdata.msgs[i].timer + delay) - 1;
                    
                    if (delay < 0) {
                        cerr<<"Error: negative delay: "<<core_id<<" "<<ins_mem.prog_id<<" "<<thread_id<<" "
                            <<ins_mem.mem_type<<" "<<ins_mem.addr_dmem<<endl;
                        MPI_Abort(MPI_COMM_WORLD, -1);
                    }
                }
                MPI_Send(&delay, 1, MPI_INT, src_proc_id , thread_id, comm);
                break;
            }
            default:{
                cerr<< "Wrong message type "<<msg.message_type<<endl;
                unlock();
                report();
                MPI_Abort(MPI_COMM_WORLD, -1);
            }
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
        typedef void * (*THREADFUNCPTR)(void *);
        int rc = pthread_create(&thread_data[t].handle, &attr, msgHandlerWrapper, (void *)(&thread_data[t].args)); 
        
        if (rc) {
            cerr << "Error return code from pthread_create(): " << rc << endl;
            MPI_Finalize();
            exit(-1);
        }
    }

    // Free attribute 
    pthread_attr_destroy(&attr);
}

void UncoreManager::collect_threads() 
{
    for(long t = 0; t < num_threads; t++) { 
        void *status;
        int rc = pthread_join(thread_data[t].handle, &status);
        if (rc) {
            cerr << "Error return code from pthread_join(): " << rc << endl;
            MPI_Finalize();
            exit(-1);
       }
       cout << "[PriME] Main: completed join with uncore thread " << t << " having a status of "<< status << endl;
    }
    getSimFinishTime();
}


