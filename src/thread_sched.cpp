//===========================================================================
// thread_sched.cpp schedules threads to cores
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

#include "thread_sched.h"
#include "common.h"


void ThreadSched::init(int num_cores_in)
{
    num_cores = num_cores_in;
    core_stat = new int [num_cores];
    assert(core_stat);
    memset(core_stat, 0, sizeof(int) * num_cores);

    core_map = new int*[num_cores];
    assert(core_map);
    memset(core_map, 0, sizeof(int*) * num_cores);

    next_empty = 0;
}


//Allocate a core for a thread
int ThreadSched::allocCore(int pid, int tid)
{
    int cur_core = next_empty;
    next_empty++;

    if (cur_core >= num_cores)
        return -1;

    core_stat[cur_core] = pid;
    if (core_map[pid] == NULL) {
        core_map[pid] = new int[num_cores];
        assert(core_map[pid]);
        memset(core_map[pid], -1, sizeof(int) * num_cores);
    }
    core_map[pid][tid] = cur_core;
    return cur_core;
}

//Return the core id for the allocated thread
int ThreadSched::getCoreId(int pid, int tid)
{
    assert(core_map[pid] != NULL);
    return core_map[pid][tid];
}

//Return the core id for the allocated thread
int ThreadSched::getProcId(int cid)
{
    return core_stat[cid];
}

ThreadSched::~ThreadSched()
{
    delete [] core_stat;
}
