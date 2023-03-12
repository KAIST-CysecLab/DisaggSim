# Copyright (c) 2012-2020 ARM Limited
# All rights reserved.
#
# The license below extends only to copyright in the software and shall
# not be construed as granting a license to any other intellectual
# property including but not limited to intellectual property relating
# to a hardware implementation of the functionality of the software
# licensed hereunder.  You may use the software subject to the license
# terms below provided that you ensure that this notice is replicated
# unmodified and in its entirety in all distributions of the software,
# modified or unmodified, in source code or in binary form.
#
# Copyright (c) 2013 Amin Farmahini-Farahani
# Copyright (c) 2015 University of Kaiserslautern
# Copyright (c) 2015 The University of Bologna
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from m5.params import *
from m5.proxy import *
from m5.objects.QoSMemCtrl import *

# Enum for memory scheduling algorithms, currently First-Come
# First-Served and a First-Row Hit then First-Come First-Served
# For disaggregated system simulation, currently only FCFS

# From MemPool.py (don't need to redefine)
class MemSched(Enum): vals = ['fcfs', 'frfcfs']

# MemCtrl is a single-channel single-ported Memory controller model
# that aims to model the most important system-level performance
# effects of a memory controller, interfacing with media specific
# interfaces

# TODO: MemPoolCtrl that supports multiple media
class MemPoolCtrl(QoSMemCtrl):
    type = 'MemPoolCtrl'
    cxx_header = "disaggregated_component/memory_module/mem_pool_ctrl.hh"

    # single-ported on the system interface side, instantiate with a
    # bus in front of the controller for multiple ports
    port = ResponsePort("This port responds to memory requests")

    # Interface to volatile, DRAM media
    # Old code
    # dram = Param.DRAMInterface(NULL, "DRAM interface")
    # New code
    # now supports multiple DRAM media: must be configured by a script
    drams = VectorParam.DisagDRAMInterface([], "DRAM media")

    # Interface to non-volatile media
    # Old code
    # nvm = Param.NVMInterface(NULL, "NVM interface")
    # New code
    # now supports multiple NVM media: must be configured by a script
    nvms = VectorParam.DisagNVMInterface([], "NVM media")

    # read and write buffer depths are set in the interface
    # the controller will read these values when instantiated

    # threshold in percent for when to forcefully trigger writes and
    # start emptying the write buffer
    write_high_thresh_perc = Param.Percent(85, "Threshold to force writes")

    # threshold in percentage for when to start writes if the read
    # queue is empty
    write_low_thresh_perc = Param.Percent(50, "Threshold to start writes")

    # minimum write bursts to schedule before switching back to reads
    min_writes_per_switch = Param.Unsigned(16, "Minimum write bursts before "
                                           "switching to reads")

    # scheduler, address map and page policy
    # Old code
    # mem_sched_policy = Param.MemSched('frfcfs', "Memory scheduling policy")
    # New code (default: fcfs)
    mem_sched_policy = Param.MemSched('fcfs', "Memory scheduling policy")

    # pipeline latency of the controller and PHY, split into a
    # frontend part and a backend part, with reads and writes serviced
    # by the queues only seeing the frontend contribution, and reads
    # serviced by the memory seeing the sum of the two
    static_frontend_latency = Param.Latency("10ns", "Static frontend latency")
    static_backend_latency = Param.Latency("10ns", "Static backend latency")

    command_window = Param.Latency("10ns", "Static backend latency")

    # New code: Memory controller CID (a.k.a. subCID)
    subCID = Param.Unsigned(9999, "Memory controller's subCID")
