//===========================================================================
// system.cpp simulates inclusive multi-level cache system with NoC and memory
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

#include "cache_stash.h"
#include <algorithm>

CacheStash::CacheStash(size_t _size) 
{
    size = _size;
    pthread_mutex_init(&mutex, NULL);
}

CacheStash::~CacheStash()
{
	pthread_mutex_destroy(&mutex);
}

bool CacheStash::contains(uint64_t addr)
{
	pthread_mutex_lock(&mutex);
	bool ret = (std::find(storage.begin(), storage.end(), addr) != storage.end());
	pthread_mutex_unlock(&mutex);
    return ret;
}

void CacheStash::insert(uint64_t addr) 
{
	pthread_mutex_lock(&mutex);
	storage.push_front(addr);
	if (storage.size() > size) {
		storage.pop_back();
	}
	pthread_mutex_unlock(&mutex);
}

void CacheStash::insert_unique(uint64_t addr) 
{
	pthread_mutex_lock(&mutex);
	if (!contains(addr)) 
		insert(addr);
	pthread_mutex_unlock(&mutex);
}

void CacheStash::eject(uint64_t addr)
{
	pthread_mutex_lock(&mutex);
	auto it = std::find(storage.begin(), storage.end(), addr);
	if (it != storage.end())
		storage.erase(it);
	pthread_mutex_unlock(&mutex);
}
