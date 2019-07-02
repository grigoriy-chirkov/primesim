//===========================================================================
// bus.cpp implements a simple bus with fix delay. The 
// contention delay is based on analytical M/G/1 queueing model
// from the MIT Graphite simulator
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
#include <cmath>
#include <string>
#include <cstring>
#include <inttypes.h>

#include "bus.h"


using namespace std;

bool Bus::init(XmlBus* xml_bus)
{
    if (xml_bus == NULL) {
      delay = 0;
      unlim_bw = false;
      data_pkt_len = 0;
      ctrl_pkt_len = 0;
    } else {
      delay = xml_bus->delay;
      unlim_bw = xml_bus->unlim_bw;
      data_pkt_len = xml_bus->data_pkt_len;
      ctrl_pkt_len = xml_bus->ctrl_pkt_len;      
    }
    
    if (!unlim_bw) {
      bus_queue = QueueModel::create("history_tree", delay);
      pthread_mutex_init(&mutex, NULL);
    }

    return true;
}

// This function returns bus contention_delay only because bus access time is 
// included in cache access time

uint64_t Bus::access(uint64_t timer, bool is_data)
{
    if (unlim_bw) {
      return 0;
    }

    int pkt_len = is_data ? data_pkt_len : ctrl_pkt_len;
    pthread_mutex_lock(&mutex);
    uint64_t contention_delay = bus_queue->computeQueueDelay(timer, pkt_len*delay); 
    pthread_mutex_unlock(&mutex);
    return contention_delay;
}


Bus::~Bus()
{
    if (!unlim_bw) {
        pthread_mutex_destroy(&mutex);
        delete bus_queue;     
    }
}
