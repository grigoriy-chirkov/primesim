//===========================================================================
// prime.cpp contains the  MPI interfaces to processes of core models
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

#include <stdio.h>
#include <assert.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <string>
#include <utility>
#include <unistd.h>
#include <map>
#include <set>
#include <vector>
#include <pthread.h> 

#include "mpi.h"
#include "uncore_manager.h"
#include "xml_parser.h"
#include "common.h"
#include "system.h"
#include "prime.h"


using namespace std;

int main(int argc, char *argv[])
{
    int rc, prov = 0;
    rc = MPI_Init_thread(&argc,&argv, MPI_THREAD_MULTIPLE, &prov);
    if (rc != MPI_SUCCESS) {
        cerr << "Error starting MPI program. Terminating." << endl;
        MPI_Abort(MPI_COMM_WORLD, rc);
    }
    if(prov != MPI_THREAD_MULTIPLE) {
        cerr << "Provide level of thread supoort is not required: " << prov << endl;
        MPI_Abort(MPI_COMM_WORLD, rc);
    }

    if(argc != 3) {
        cerr<<"usage: "<< argv[0] <<" config_file output_file\n";
        cerr<<"argc is:" << argc <<endl;
        MPI_Abort(MPI_COMM_WORLD, -1);
        return -1;
    }

    XmlParser xml_parser;
    if( !xml_parser.parse(argv[1]) ) {
        cerr<< "XML file parse error!\n";
        MPI_Abort(MPI_COMM_WORLD, -1);
        return -1;
    }
    const XmlSim* xml_sim = xml_parser.getXmlSim();

    UncoreManager* uncore_manager = new UncoreManager;
    uncore_manager->init(xml_sim);
    uncore_manager->spawn_threads();
    uncore_manager->collect_threads();
    uncore_manager->report(argv[2]);
    delete uncore_manager;

    MPI_Finalize();
}
