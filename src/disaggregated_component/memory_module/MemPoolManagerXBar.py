# Copyright (c) 2012, 2015, 2017, 2019-2020 ARM Limited
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
# Copyright (c) 2005-2008 The Regents of The University of Michigan
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

from m5.objects.System import System
from m5.params import *
from m5.proxy import *
from m5.SimObject import SimObject

from m5.objects.ClockedObject import ClockedObject

# Memory Pool Manager class (a modified NoncoherentXBar)
# The original NoncoherentXBar inherits BaseXBar, but this class
# doesn't. (It contains all members required in itself.)
class MemPoolManagerXBar(ClockedObject):
    type = 'MemPoolManagerXBar'
    cxx_header = "disaggregated_component/memory_module/" + \
        "mem_pool_manager_xbar.hh"

    cpu_side_ports = VectorResponsePort("Vector port for connecting "
                                                "mem side ports")
    slave    = DeprecatedParam(cpu_side_ports,
                                '`slave` is now called `cpu_side_ports`')
    mem_side_ports = VectorRequestPort("Vector port for connecting "
                                                "cpu side ports")
    master   = DeprecatedParam(mem_side_ports,
                                '`master` is now called `mem_side_ports`')

    # Latencies governing the time taken for the variuos paths a
    # packet has through the crossbar. Note that the crossbar itself
    # does not add the latency due to assumptions in the coherency
    # mechanism. Instead the latency is annotated on the packet and
    # left to the neighbouring modules.
    #
    # A request incurs the frontend latency, possibly snoop filter
    # lookup latency, and forward latency. A response incurs the
    # response latency. Frontend latency encompasses arbitration and
    # deciding what to do when a request arrives. the forward latency
    # is the latency involved once a decision is made to forward the
    # request. The response latency, is similar to the forward
    # latency, but for responses rather than requests.

    # NOTE: Default values are from the original IOXBar
    frontend_latency = Param.Cycles(2, "Frontend latency")
    forward_latency = Param.Cycles(1, "Forward latency")
    response_latency = Param.Cycles(2, "Response latency")

    # The XBar uses one Layer per requestor. Each Layer forwards a packet
    # to its destination and is occupied for header_latency + size /
    # width cycles
    header_latency = Param.Cycles(1, "Header latency")

    # Width governing the throughput of the crossbar
    # NOTE: Default values are from the original IOXBar
    width = Param.Unsigned(16, "Datapath width per port (bytes)")

    # The default port can be left unconnected, or be used to connect
    # a default response port
    default = RequestPort("Port for connecting an optional default responder")

    # The default port can be used unconditionally, or based on
    # address range, in which case it may overlap with other
    # ports. The default range is always checked first, thus creating
    # a two-level hierarchical lookup. This is useful e.g. for the PCI
    # xbar configuration.

    # Old code: it now uses CID instead of range-based routing
    # use_default_range = Param.Bool(False, "Perform address mapping for " \
    #                                    "the default port")
