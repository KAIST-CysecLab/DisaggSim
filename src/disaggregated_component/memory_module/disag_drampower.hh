/*
 * Copyright (c) 2014 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 * DisagDRAMPower declaration
 */

#ifndef __MEM_DISAG_DRAM_POWER_HH__
#define __MEM_DISAG_DRAM_POWER_HH__

#include "libdrampower/LibDRAMPower.h"
// #include "params/DRAMInterface.hh"
#include "params/DisagDRAMInterface.hh"

/**
 * DRAMPower is a standalone tool which calculates the power consumed by a
 * DRAM in the system. This class wraps the DRAMPower library.
 */
// The same class as the original DRAMPower,
// except that it requires DisagDRAMInterfaceParams
// Instead of DRAMInterfaceParams
// class DRAMPower
class DisagDRAMPower
{

 private:

    /**
     * Transform the architechture parameters defined in
     * DisagDRAMInterfaceParams to the memSpec of DRAMPower
     */
    static Data::MemArchitectureSpec getArchParams(
                                     const DisagDRAMInterfaceParams* p);

    /**
     * Transforms the timing parameters defined in DisagDRAMInterfaceParams to
     * the memSpec of DRAMPower
     */
    static Data::MemTimingSpec getTimingParams(const DisagDRAMInterfaceParams* p);

    /**
     * Transforms the power and current parameters defined in
     * DisagDRAMInterfaceParams to the memSpec of DRAMPower
     */
    static Data::MemPowerSpec getPowerParams(const DisagDRAMInterfaceParams* p);

    /**
     * Determine data rate, either one or two.
     */
    static uint8_t getDataRate(const DisagDRAMInterfaceParams* p);

    /**
     * Determine if DRAM has two voltage domains (or one)
     */
    static bool hasTwoVDD(const DisagDRAMInterfaceParams* p);

    /**
     * Return an instance of MemSpec based on the DisagDRAMInterfaceParams
     */
    static Data::MemorySpecification getMemSpec(const DisagDRAMInterfaceParams* p);

 public:

    // Instance of DRAMPower Library
    libDRAMPower powerlib;

    DisagDRAMPower(const DisagDRAMInterfaceParams* p, bool include_io);

};

#endif //__MEM_DISAG_DRAM_POWER_HH__

