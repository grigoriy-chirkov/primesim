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
#include <cassert>
#include <unistd.h>

#include "uncore_manager.h"
#include "common.h"

using namespace std;

// Handle receiving MPI messages and send back responses
void *msgProducerWrapper(void *args)
{
    HandlerArgs* targs = (HandlerArgs*) args;
    targs->uncore_manager->msgProducer(targs->tid);
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
    sys = new System;
    assert(sys != NULL);
    sys->init(xml_sim->sys);
    thread_sched = new ThreadSched;
    assert(thread_sched != NULL);
    thread_sched->init(getCoreCount());

    max_msg_size = xml_sim->max_msg_size;
    num_cons_threads = xml_sim->num_cons_threads;
    num_prod_threads = xml_sim->num_prod_threads;
    thread_sync_interval = xml_sim->thread_sync_interval;
    cpi_nonmem = xml_sim->sys->cpi_nonmem;
    freq = xml_sim->sys->freq;

    pthread_mutex_init(&mutex, NULL);

    consumers = new HandlerData[num_cons_threads];
    assert(consumers);

    producers = new HandlerData[num_prod_threads];
    assert(producers);

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

    // Match call to createCommWithoutUncore here with same call in core_manager, 
    // otherwise process will hang forever
    MPI_Comm   barrier_comm;
    createCommWithoutUncore(comm, &barrier_comm);

    segment_cnt = new std::vector<int>;
    assert(segment_cnt);
}

void UncoreManager::alloc_server()
{
    MPIMsg msg;
    memset(&msg, 0, sizeof(MPIMsg));

    while (1) {
        MPI_Recv(&msg, sizeof(MPIMsg), MPI_CHAR, MPI_ANY_SOURCE, getCoreCount(), comm, MPI_STATUS_IGNORE);
        switch (msg.message_type) {
            case PROCESS_STARTING: { //Receive a msg indicating a new process                
                cout << "[PriME] Pin process " << msg.pid << " begins" << endl;
                proc_num++;
                break;
            }
            case PROCESS_FINISHING: {
                proc_num--;
                cout << "[PriME] Pin process " << msg.pid << " finishes"<<endl;
                if (proc_num == 0) 
                    return;
                break;
            }
            case NEW_THREAD: {//Receive a msg indicating a new thread
                int cid = allocCore(msg.pid, msg.tid);   
                if (cid < 0) {
                    cerr << "Not enough cores for process " << msg.pid << " thread " << msg.tid << endl;
                    MPI_Abort(MPI_COMM_WORLD, -1);
                } 
                auto& cdata = core_data[cid];
                cdata.init(cid, msg.pid, msg.tid, getCycle(), max_msg_size);
                MPI_Send(&cid, 1, MPI_INT, msg.pid, msg.tid, comm);
                cdata.insert_msg(&msg, 1);
                break;
            }
            default: {
                cerr << "Wrong msg type in allocServer from process " << msg.pid << " thread " << msg.tid << endl;
                MPI_Abort(MPI_COMM_WORLD, -1);
            }
        }
    }
}

void UncoreManager::msgProducer(int my_tid) 
{
    MPIMsg* inbuffer = new MPIMsg[max_msg_size + 1];
    assert(inbuffer != NULL);
    memset(inbuffer, 0, sizeof(MPIMsg)*(max_msg_size + 1));
    auto& msg = inbuffer[0];

    while (1) {
        bool throttle = true;
        for (int cid = my_tid; cid < getCoreCount(); cid += num_prod_threads) {
            auto& cdata = core_data[cid];
            if (!cdata.valid)
                continue;

            // Check that we have msg incoming
            int msg_here = 0;
            MPI_Status status;
            MPI_Iprobe(MPI_ANY_SOURCE, cid, comm, &msg_here, &status);
            if (!msg_here) 
                continue;

            // Check that we have place to store the message
            int msg_size = 0;
            MPI_Get_count(&status, MPI_CHAR, &msg_size);
            unsigned msg_count = msg_size / sizeof(MPIMsg);
            if (cdata.empty_count() < msg_count)
                continue;

            // Finally receive messge
            MPI_Recv(inbuffer, (max_msg_size+1) * sizeof(MPIMsg), MPI_CHAR, MPI_ANY_SOURCE, cid, comm, MPI_STATUS_IGNORE);
            throttle = false;

            switch (msg.message_type) {
                case TERMINATE: {
                    delete[] inbuffer;
                    pthread_exit(NULL);
                }
                default: {
                    assert(cdata.finished != true || msg.message_type != MEM_REQUESTS);
                    cdata.insert_msg(inbuffer, msg.payload_len);
                    break;
                }
            }
        }
        if (throttle) {
            usleep(1);
        }
    }
}

void UncoreManager::msgConsumer(int my_tid) 
{
    const uint64_t batch_size = 10;

    MPIMsg* msg_buffer = new MPIMsg[batch_size];
    assert(msg_buffer != NULL);
    memset(msg_buffer, 0, sizeof(MPIMsg)*batch_size);

    while (1) {
        for (int cid = my_tid; cid < getCoreCount(); cid += num_cons_threads) {
            auto& cdata = core_data[cid];
            if (!cdata.valid)
                continue;

            if (cdata.segment > cur_segment) 
                continue;

            uint64_t msg_num = cdata.eject_msg(msg_buffer, batch_size);
            if (msg_num == 0) 
                continue;

            for (uint64_t i = 0; i < msg_num; i++) {
                MPIMsg& msg = msg_buffer[i];

                if (msg.is_control) {
                    switch (msg.message_type) {
                        case NEW_THREAD: {
                            lock();
                            num_threads_live++;
                            cout << "[PriME] Pin thread " << msg.tid << " from process " << msg.pid << " begins in core " << cid << endl;
                            unlock();
                            break;
                        }
                        case THREAD_FINISHING: {
                            lock();
                            num_threads_live--;
                            cdata.finished = true;
                            cout << "[PriME] Thread " << msg.tid << " from process " << msg.pid << " finishes" << endl;
                            unlock();
                            break;
                        }
                        case THREAD_LOCK:{
                            lock();
                            num_threads_live--;
                            unlock();
                            break;
                        }
                        case THREAD_UNLOCK: {
                            lock();
                            cdata.cycle = getCycle();
                            num_threads_live++;
                            unlock();
                            break;
                        }
                        case MEM_REQUESTS: {
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
            }

            uint64_t new_seg = cdata.cycle / thread_sync_interval;
            if (new_seg > cdata.segment) {
                lock();
                reserveSegmentCntSpace(new_seg);
                for (uint64_t i = cdata.segment; i < new_seg; i++) {
                    segment_cnt->at(i) += 1;
                }
                unlock();                
            }
            cdata.segment = new_seg;
        }

        if (num_threads_live == 0 && proc_num == 0 && cur_segment > 0) {
            delete [] msg_buffer; 
            pthread_exit(NULL);
        }

        lock();
        reserveSegmentCntSpace(cur_segment);
        if (segment_cnt->at(cur_segment) >= max(num_threads_live, 1)) {
                cur_segment++;
                if (cur_segment % 10 == 0)
                    cout << "[PriME] Segment " << cur_segment << " finished" << endl;
        }
        unlock();
    }
}

void UncoreManager::reserveSegmentCntSpace(uint64_t num) 
{
    if (segment_cnt->size() <= num+1) {
        segment_cnt->resize(2*num+1, 0);
    }
}


void UncoreManager::getSimStartTime()
{
    struct timespec buffer;
    clock_gettime(CLOCK_REALTIME, &buffer);
    sim_start_time = double(buffer.tv_sec) + double(buffer.tv_nsec) / 1000000000.0; 
}

void UncoreManager::getSimFinishTime()
{
    struct timespec buffer;
    clock_gettime(CLOCK_REALTIME, &buffer);
    sim_finish_time = double(buffer.tv_sec) + double(buffer.tv_nsec) / 1000000000.0; 
}


int UncoreManager::allocCore(int pid, int tid)
{
    return thread_sched->allocCore(pid, tid);
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
    double sim_time = sim_finish_time - sim_start_time;
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

UncoreManager::~UncoreManager()
{
    //in->close();
    delete [] producers;
    delete [] consumers;
    delete [] core_data;
    delete sys;
    delete thread_sched;
    delete segment_cnt;
    pthread_mutex_destroy(&mutex);
    MPI_Comm_free(&comm);
}     

void UncoreManager::spawn_threads() 
{
    getSimStartTime();
    for(int t = 0; t < num_cons_threads; t++) {
        cout << "[PriME] Main: creating consumer thread " << t <<endl;
        consumers[t].init(this, t);
        int rc = pthread_create(&consumers[t].handle, NULL, msgConsumerWrapper, (void *)(&consumers[t].args)); 
        
        if (rc) {
            cerr << "Error return code from pthread_create(): " << rc << endl;
            MPI_Abort(MPI_COMM_WORLD, -1);
        }
    }

    for(int t = 0; t < num_prod_threads; t++) {
        cout << "[PriME] Main: creating producer thread " << t <<endl;
        producers[t].init(this, t);
        int rc = pthread_create(&producers[t].handle, NULL, msgProducerWrapper, (void *)(&producers[t].args)); 
        
        if (rc) {
            cerr << "Error return code from pthread_create(): " << rc << endl;
            MPI_Abort(MPI_COMM_WORLD, -1);
        }
    } 
}

void UncoreManager::collect_threads() 
{
    MPIMsg term_msg;
    term_msg.is_control = true;
    term_msg.message_type = TERMINATE;
    term_msg.payload_len = 1;
    
    void *status;
    for(int t = 0; t < num_cons_threads; t++) { 
        int rc = pthread_join(consumers[t].handle, &status);
        if (rc) {
            cerr << "Error return code from pthread_join(): " << rc << endl;
            MPI_Abort(MPI_COMM_WORLD, -1);
        }
        cout << "[PriME] Main: completed join with consumer thread " << t << " having a status of "<< status << endl;
    }

    for(int t = 0; t < num_prod_threads; t++) { 
        MPI_Send(&term_msg, sizeof(MPIMsg), MPI_CHAR, 0, t, comm);
        int rc = pthread_join(producers[t].handle, &status);
        if (rc) {
            cerr << "Error return code from pthread_join(): " << rc << endl;
            MPI_Abort(MPI_COMM_WORLD, -1);
        }
        cout << "[PriME] Main: completed join with producer thread " << t << " having a status of "<< status << endl;
    }
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

void CoreData::init(int _cid, int _pid, int _tid, uint64_t _cycle, int max_msg_size) 
{
    const int BUF_SIZE = 10;
    max_count = (max_msg_size + 1) * BUF_SIZE;
    msgs = new MPIMsg[max_count];
    assert(msgs != NULL);
    memset(msgs, 0, max_count*sizeof(MPIMsg));

    valid = true;
    cid = _cid;
    pid = _pid;
    tid = _tid;
    cycle = _cycle;

    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&can_produce, NULL);
};

CoreData::~CoreData() 
{
    if (valid) {
        delete [] msgs;
        pthread_mutex_destroy(&mutex);
        pthread_cond_destroy(&can_produce);
    }
}


void CoreData::insert_msg(const MPIMsg* inbuffer, size_t num) 
{
    pthread_mutex_lock(&mutex);
    while(count + num > max_count) { // full
    // wait until some elements are consumed
        pthread_cond_wait(&can_produce, &mutex);
    }

    if (num > max_count - in_pos){
        uint64_t part1 = max_count - in_pos;
        uint64_t part2 = num - part1;    
        memcpy(msgs+in_pos, inbuffer, sizeof(MPIMsg)*part1);
        memcpy(msgs, inbuffer+part1, sizeof(MPIMsg)*part2);  
    } else {
        memcpy(msgs+in_pos, inbuffer, sizeof(MPIMsg)*num);
    }

    count += num;
    in_pos = (in_pos + num) % max_count;
    pthread_mutex_unlock(&mutex);
}

size_t CoreData::eject_msg(MPIMsg* outbuffer, size_t num) 
{
    pthread_mutex_lock(&mutex);
    if (count == 0) {
        pthread_mutex_unlock(&mutex);
        return 0;
    }

    size_t out_num = min(num, count);
    if (out_num > max_count - out_pos){
        uint64_t part1 = max_count - out_pos;
        uint64_t part2 = out_num - part1;    
        memcpy(outbuffer, msgs+out_pos, sizeof(MPIMsg)*part1);
        memcpy(outbuffer+part1, msgs, sizeof(MPIMsg)*part2);  
    } else {
        memcpy(outbuffer, msgs+out_pos, sizeof(MPIMsg)*out_num);
    }
    out_pos = (out_pos + out_num) % max_count;
    count -= out_num;

    pthread_cond_signal(&can_produce);
    pthread_mutex_unlock(&mutex);
    return out_num;
}

size_t CoreData::empty_count()
{
    return max_count - count;
}

void CoreData::report(ofstream& report_ofstream) 
{
    uint64_t ins_count = ins_mem + ins_nonmem;
    uint64_t cycle_count = nonmem_cycles + mem_cycles;
    report_ofstream << "---------------------------------------------------------\n";
    report_ofstream << "Core " <<cid<< " runs " << ins_count << " instructions in " << cycle_count <<" cycles" << endl;
    report_ofstream << "Core " <<cid<< " average IPC = "<< double(ins_count) / double(cycle_count) << endl;
    report_ofstream << "Core " <<cid<< " memory instructions: "<< ins_mem <<endl;
    report_ofstream << "Core " <<cid<< " memory access cycles: "<< mem_cycles <<endl;
    report_ofstream << "Core " <<cid<< " non-memory instructions: "<< ins_nonmem <<endl;
    report_ofstream << "Core " <<cid<< " non-memory cycles: "<< nonmem_cycles <<endl;
}

