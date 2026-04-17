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

#ifndef DEBUG_H_
#define DEBUG_H_

//This header has common debugging datastructure defs.

/* Describes the addresses at which a shared object is loaded. GDB needs this. */
struct LibInfo {
    void* textAddr;
    void* bssAddr;
    void* dataAddr;
};

/* Minimal info for any shared library we want to load symbols for */
struct LibSymbolInfo {
    // Full path to the .so as loaded by the runtime loader
    char path[512];
    // Section base addresses in the target process
    void* textAddr;
    void* dataAddr;
    void* bssAddr;
    // 1 if present/resolved, 0 otherwise
    int present;
};

/* Aggregate structure with a small fixed list of DSOs we care about */
struct DebugLibsInfo {
    // libs[0] is always libzsim.so if present
    // subsequent entries may be dramsim3, ramulator2, dramsim, ramulator, etc.
    LibSymbolInfo libs[6];
    int count;
};

#endif  // DEBUG_H_
