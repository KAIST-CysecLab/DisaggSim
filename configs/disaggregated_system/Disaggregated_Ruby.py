from __future__ import print_function

import math
import m5
from m5.objects import *
from m5.defines import buildEnv
from m5.util import addToPath, fatal

addToPath('../')

from common import ObjectList
from common import FileSystemConfig

import DISAGGREGATED

from topologies import *
from network import Network

def define_options(parser):
    # By default, ruby uses the simple timing cpu
    parser.set_defaults(cpu_type="TimingSimpleCPU")

    parser.add_option("--ruby-clock", action="store", type="string",
                      default='2GHz',
                      help="Clock for blocks running at Ruby system's speed")

    parser.add_option("--access-backing-store", action="store_true", default=False,
                      help="Should ruby maintain a second copy of memory")

    # Options related to cache structure
    parser.add_option("--ports", action="store", type="int", default=4,
                      help="used of transitions per cycle which is a proxy \
                            for the number of ports.")

    # network options are in network/Network.py

    # ruby mapping options
    parser.add_option("--numa-high-bit", type="int", default=0,
                      help="high order address bit to use for numa mapping. " \
                           "0 = highest bit, not specified = lowest bit")

    parser.add_option("--recycle-latency", type="int", default=10,
                      help="Recycle latency for ruby controller input buffers")


    # use ruby interconnect for disaggregation
    parser.add_option("--d_ruby", action="store_true")
    parser.add_option("--d-ruby-clock", action="store", type="string", default='2GHz', help="Clock for d_ruby")

    DISAGGREGATED.define_options(parser)
    Network.define_options(parser)

def create_system(options, full_system, d_system, num_sequencers, piobus = None, dma_ports = [], bootmem=None):

    assert(not full_system)
    assert(buildEnv['PROTOCOL'] == 'DISAGGREGATED')

    ruby = DisaggregatedRubySystem()
    d_system.ruby = ruby

    # Create the network object
    (network, IntLinkClass, ExtLinkClass, RouterClass, InterfaceClass) = \
        Network.create_network(options, ruby)
    ruby.network = network

    ruby.clk_domain = SrcClockDomain(clock = options.d_ruby_clock, voltage_domain = VoltageDomain())

    (module_sequencers, module_controllers, topology) = DISAGGREGATED.create_system(options, ruby, num_sequencers)

    # Create the network topology
    topology.makeTopology(options, network, IntLinkClass, ExtLinkClass, RouterClass)

    # Register the topology elements with faux filesystem (SE mode only)
    if not full_system:
        topology.registerTopology(options)

    # Initialize network based on topology
    Network.init_network(options, network, InterfaceClass)

    # TODO Check
    # setup_memory_controllers(system, ruby, dir_cntrls, options)
    ruby.block_size_bytes = options.cacheline_size
    ruby.memory_size_bits = 48

    ruby.number_of_virtual_networks = ruby.network.number_of_virtual_networks
    ruby.num_of_sequencers = len(module_sequencers)
    return (module_sequencers,module_controllers)
