//===========================================================================
// pin_wrapper.cpp
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

#include "common.h"
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <fcntl.h>
#include <cassert>
#include <thread>
#include <cstring>
#include "mpi.h"
#include "pin_wrapper.h"
#include "retranslator.h"
#include <errno.h>

using namespace std;

int main(int argc, char** argv)
{
    int pid = -1;
    int max_msg_size = 1024;
    tie(pid, max_msg_size) = parse_args(argc, argv);
    // Fork must be executed BEFORE MPI_Init
    switch (fork()) { 
        case -1: // error
            cerr << "Couldn't fork Pin process" << endl;
            MPI_Abort(MPI_COMM_WORLD, -1);
        case 0: // child
            run_pin(argc, argv, pid, max_msg_size);
        default: // parent
            break; 
    }
    assert(pid == init_mpi(&argc, &argv));

    {
        auto retranslator = make_unique<Retranslator>(pid, max_msg_size);
        retranslator->main_server();
        retranslator->collect_threads();
    }

    MPI_Finalize();
}


void usage() {
    cout << "Usage: ./pipe2mpi -l <max_msg_size> -p <program name>" << endl;
}

tuple<int, int>
parse_args(int argc, char** argv) {
    int max_msg_size = 1024;
    int pid = -1;
    int c = -1;

    while ((c = getopt(argc, argv, "l:p:")) != -1) {        
        switch (c) {
            case 'l':
                max_msg_size = stoi(optarg);
                break;
            case 'p':
                pid = stoi(optarg);
                break;
            case '?':
                usage();
                MPI_Abort(MPI_COMM_WORLD, -1);
        }
    }
    return {pid, max_msg_size};
}

void run_pin(int argc, char** argv, int pid, int max_msg_size) {
    constexpr int max_args = 100;
    char* new_argv[max_args];
    new_argv[0] = strdup(PIN); new_argv[1] = strdup("-ifeellucky");
    new_argv[2] = strdup("-t"); new_argv[3] = strdup(PRIMELIB);
    new_argv[4] = strdup("-l"); new_argv[5] = strdup(to_string(max_msg_size).c_str());
    new_argv[6] = strdup("-p"); new_argv[7] = strdup(to_string(pid).c_str());
    new_argv[8] = strdup("--"); 
    int cur_idx = 9;
    for(; optind < argc; optind++){      
        assert(cur_idx < max_args);
        new_argv[cur_idx] = argv[optind];
        cur_idx++;
    } 
    assert(cur_idx < max_args);
    new_argv[cur_idx] = NULL;

    int rc = execvp(PIN, new_argv);
    assert(rc > 0); // should never end up here
}
