/** $lic$
 * Copyright (C) 2012-2015 by Massachusetts Institute of Technology
 * Copyright (C) 2010-2013 by The Board of Trustees of Stanford University
 *
 * This file is part of zsim.
 *
 * zsim is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, version 2.
 *
 * If you use this software in your research, we request that you reference
 * the zsim paper ("ZSim: Fast and Accurate Microarchitectural Simulation of
 * Thousand-Core Systems", Sanchez and Kozyrakis, ISCA-40, June 2013) as the
 * source of the simulator in any publications that use this software, and that
 * you send us a citation of your work.
 *
 * zsim is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef FILTER_CACHE_H_
#define FILTER_CACHE_H_

#include "bithacks.h"
#include "cache.h"
#include "galloc.h"
#include "zsim.h"

// Ziqi: Serializing access in real-time order
extern uint64_t core_serial;

/* Extends Cache with an L0 direct-mapped cache, optimized to hell for hits
 *
 * L1 lookups are dominated by several kinds of overhead (grab the cache locks,
 * several virtual functions for the replacement policy, etc.). This
 * specialization of Cache solves these issues by having a filter array that
 * holds the most recently used line in each set. Accesses check the filter array,
 * and then go through the normal access path. Because there is one line per set,
 * it is fine to do this without grabbing a lock.
 */

class FilterCache : public Cache {
    private:
        struct FilterEntry {
            volatile Address rdAddr;
            volatile Address wrAddr;
            volatile uint64_t availCycle;

            void clear() {wrAddr = 0; rdAddr = 0; availCycle = 0;}
        };

        //Replicates the most accessed line of each set in the cache
        FilterEntry* filterArray;
        Address setMask;
        uint32_t numSets;
        uint32_t srcId; //should match the core
        uint32_t reqFlags;

        lock_t filterLock;
        uint64_t fGETSHit, fGETXHit;

    public:
        FilterCache(uint32_t _numSets, uint32_t _numLines, CC* _cc, CacheArray* _array,
                ReplPolicy* _rp, uint32_t _accLat, uint32_t _invLat, g_string& _name)
            : Cache(_numLines, _cc, _array, _rp, _accLat, _invLat, _name)
        {
            numSets = _numSets;
            setMask = numSets - 1;
            filterArray = gm_memalign<FilterEntry>(CACHE_LINE_BYTES, numSets);
            for (uint32_t i = 0; i < numSets; i++) filterArray[i].clear();
            futex_init(&filterLock);
            fGETSHit = fGETXHit = 0;
            srcId = -1;
            reqFlags = 0;
        }

        void setSourceId(uint32_t id) {
            srcId = id;
        }

        void setFlags(uint32_t flags) {
            reqFlags = flags;
        }

        void initStats(AggregateStat* parentStat) {
            AggregateStat* cacheStat = new AggregateStat();
            cacheStat->init(name.c_str(), "Filter cache stats");

            ProxyStat* fgetsStat = new ProxyStat();
            fgetsStat->init("fhGETS", "Filtered GETS hits", &fGETSHit);
            ProxyStat* fgetxStat = new ProxyStat();
            fgetxStat->init("fhGETX", "Filtered GETX hits", &fGETXHit);
            cacheStat->append(fgetsStat);
            cacheStat->append(fgetxStat);

            initCacheStats(cacheStat);
            parentStat->append(cacheStat);
        }

        inline uint64_t load(Address vAddr, uint64_t curCycle) {
            // Ziqi: vLineAddr is shifted tag
            Address vLineAddr = vAddr >> lineBits;
            uint64_t line_addr = vAddr & UTIL_CACHE_LINE_MSB_MASK;
            uint32_t idx = vLineAddr & setMask;
            uint64_t availCycle = filterArray[idx].availCycle; //read before, careful with ordering to avoid timing races
            if (vLineAddr == filterArray[idx].rdAddr) {
                fGETSHit++;
                // Ziqi: Insert load
                // Note that vAddr is also collected from the memory trace - it is not necessarily aligned
                // L0 hit - no need to worry about evictions
                if(this->level != -1U) {
                    /*
                    if(line_addr == 0x7FFFF7DD9F00UL) {
                        nvoverlay_printf("Load 0x%lX hits L0 filter cache (record = %lu)\n", 
                            line_addr, tracer_get_record_count(nvoverlay_get_tracer(zinfo->nvoverlay)));
                    }
                    */
                    nvoverlay_intf.load_cb(zinfo->nvoverlay, this->id, line_addr, curCycle, core_serial++);
                }
                return MAX(curCycle, availCycle);
            } else {
                // Ziqi: This may generate evictions (or not generate evictions)
                uint64_t ret = replace(vLineAddr, idx, true, curCycle);
                if(this->level != -1U) {
                    // Ziqi: Note we use return value from L1 access()
                    /*
                    if(line_addr == 0x7FFFF7DD9F00UL) {
                        nvoverlay_printf("Load 0x%lX misses L0 filter cache (record = %lu)\n", 
                            line_addr, tracer_get_record_count(nvoverlay_get_tracer(zinfo->nvoverlay)));
                    }
                    */
                    nvoverlay_intf.load_cb(zinfo->nvoverlay, this->id, line_addr, ret, core_serial++);
                }
                return ret;
            }
        }

        inline uint64_t store(Address vAddr, uint64_t curCycle) {
            Address vLineAddr = vAddr >> lineBits;
            uint64_t line_addr = vAddr & UTIL_CACHE_LINE_MSB_MASK;
            uint32_t idx = vLineAddr & setMask;
            uint64_t availCycle = filterArray[idx].availCycle; //read before, careful with ordering to avoid timing races
            if (vLineAddr == filterArray[idx].wrAddr) {
                fGETXHit++;
                // Ziqi: Insert store
                // Cache store hit - do not need to worry about evictions
                if(this->level != -1U) {
                    nvoverlay_intf.store_cb(zinfo->nvoverlay, this->id, line_addr, curCycle, core_serial++);
                }
                //NOTE: Stores don't modify availCycle; we'll catch matches in the core
                //filterArray[idx].availCycle = curCycle; //do optimistic store-load forwarding
                return MAX(curCycle, availCycle);
            } else {
                // Ziqi: This may generate evictions (or not generate evictions)
                uint64_t ret = replace(vLineAddr, idx, false, curCycle);
                if(this->level != -1U) {
                    // Ziqi: Note we use return value from L1 access() method to order the store after eviction
                    nvoverlay_intf.store_cb(zinfo->nvoverlay, this->id, line_addr, ret, core_serial++);
                }
                return ret;
            }
        }

        uint64_t replace(Address vLineAddr, uint32_t idx, bool isLoad, uint64_t curCycle) {
            Address pLineAddr = procMask | vLineAddr;
            // procMask = 0x0
            //nvoverlay_error("procMask 0x%lX vline 0x%lX pline 0x%lX\n", procMask, vLineAddr, pLineAddr);
            MESIState dummyState = MESIState::I;
            futex_lock(&filterLock);
            MemReq req = {pLineAddr, isLoad? GETS : GETX, 0, &dummyState, curCycle, &filterLock, dummyState, srcId, reqFlags};
            uint64_t respCycle  = access(req);

            //Due to the way we do the locking, at this point the old address might be invalidated, but we have the new address guaranteed until we release the lock

            //Careful with this order
            Address oldAddr = filterArray[idx].rdAddr;
            filterArray[idx].wrAddr = isLoad? -1L : vLineAddr;
            filterArray[idx].rdAddr = vLineAddr;

            //For LSU simulation purposes, loads bypass stores even to the same line if there is no conflict,
            //(e.g., st to x, ld from x+8) and we implement store-load forwarding at the core.
            //So if this is a load, it always sets availCycle; if it is a store hit, it doesn't
            if (oldAddr != vLineAddr) filterArray[idx].availCycle = respCycle;

            futex_unlock(&filterLock);
            return respCycle;
        }

        uint64_t invalidate(const InvReq& req) {
            Cache::startInvalidate();  // grabs cache's downLock
            futex_lock(&filterLock);
            uint32_t idx = req.lineAddr & setMask; //works because of how virtual<->physical is done...
            if ((filterArray[idx].rdAddr | procMask) == req.lineAddr) { //FIXME: If another process calls invalidate(), procMask will not match even though we may be doing a capacity-induced invalidation!
                filterArray[idx].wrAddr = -1L;
                filterArray[idx].rdAddr = -1L;
            }
            uint64_t respCycle = Cache::finishInvalidate(req); // releases cache's downLock
            futex_unlock(&filterLock);
            return respCycle;
        }

        void contextSwitch() {
            futex_lock(&filterLock);
            for (uint32_t i = 0; i < numSets; i++) filterArray[i].clear();
            futex_unlock(&filterLock);
        }
};

#endif  // FILTER_CACHE_H_
