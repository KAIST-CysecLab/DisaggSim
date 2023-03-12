from __future__ import print_function
from __future__ import absolute_import

import m5
from m5.defines import buildEnv
from m5.objects import *
from m5.params import NULL
from m5.util import addToPath, fatal, warn

addToPath('../')

from common import MemConfig
from common import ObjectList
import InterconnectLogicConfig

def define_options(parser):
    """Parses commandline options for memory configuration"""

    parser.add_option("--num_mem_ctrls", type="int", default=2)
    parser.add_option("--num_mem_modules", type="int", default=2)
    parser.add_option("--size_mem_module", type="int", default=256)
    parser.add_option("--d_mem_size", type="string", default='1024MB')
    parser.add_option("--d_mem_voltage", type="string", default='1.0V')
    parser.add_option("--d_mem_clock", type="string", default='1GHz')
    parser.add_option("--d_logic_media_frontend_latency",
                        type="int", default=1)
    parser.add_option("--d_logic_media_forward_latency", type="int", default=0)
    parser.add_option("--d_logic_media_response_latency", type="int", default=1)
    parser.add_option("--d_logic_mem_pool_latency", type="string", default='40ns')
    parser.add_option("--mem_interleave", default=False, action='store_true')
    parser.add_option("--interleave_unit", type="int", default=4096)
    parser.add_option("--num_mem_pool_logic_processing_units", type="int", default=1)

def make_addr_space(size, num_mem_modules):
    """
    Make address space for each memory module in a memory controller
    and returns the list of address spaces

    size: the size of each memory module, MB unit, integer
    num_mem_modules: the number of memory modules
    e.g., (512, 2) -> [0-512MB, 512MB-1024MB]
    """

    assert(size >= 0)

    addr_space = []

    start, end = 0, size
    for i in range(num_mem_modules):
        addr_start = Addr(str(start) + 'MB')
        addr_end = Addr(str(end) + 'MB')
        addr_range = AddrRange(addr_start, addr_end)

        addr_space.append(addr_range)

        start, end = start + size, end + size

    return addr_space

# New memory module configuration
def create_memory_pool(options, mem_mode):
    """
    Config each memory pool controller in disaggregated system,
    and attach them to memory pool manager,
    and returns the list of memory modules that includes them.
    """

    # Sanity check
    d_mem_size = int(options.d_mem_size[:-2])
    allocated_d_mem_size = int(options.allocated_d_mem_size[:-2])
    assert(d_mem_size == options.num_mem_ctrls * options.num_mem_modules * options.size_mem_module)
    assert(d_mem_size >= allocated_d_mem_size)
    if d_mem_size > allocated_d_mem_size:
        print("Warning: options.d_mem_size > options.allocated_d_mem_size")
        print("    (Not all physical memory modules are used)")

    # One memory module that contains multiple memory pool controllers
    # and one memory pool manager
    memory_module = MemoryModule(mem_mode = mem_mode, cache_line_size = options.cacheline_size, workload = NULL)
    memory_module.voltage_domain = \
        VoltageDomain(voltage = options.d_mem_voltage)
    memory_module.clk_domain = \
        SrcClockDomain(
            clock = options.d_mem_clock,
            voltage_domain = memory_module.voltage_domain
        )
    memory_module.memories = []

    # One memory pool manager
    # Currently default parameters are hard-coded in its Python file
    mem_pool_manager = MemPoolManagerXBar()

    # Create memory controllers
    # Currently parameters are hard-coded
    # (# of CPUs) x (# of MemCtrls / CPU)
    nbr_mem_ctrls = options.num_cpu_modules * options.num_mem_ctrls
    mem_sub_module_list = []

    for i in range(nbr_mem_ctrls):

        # Create a memory sub module
        mem_sub_module = MemorySubModule(mem_mode = mem_mode, cache_line_size = options.cacheline_size, workload = NULL)

        # Create a memory pool controller
        mem_ctrl = MemPoolCtrl()

        # Memory pool controller's subCID
        # Currently hard-coded (0 - nbr_mem_ctrls-1)
        mem_ctrl.subCID = i

        # Each memory pool controller has multiple DRAM modules
        # Create address space for each memory pool controller
        mem_size = options.size_mem_module
        num_mem_modules = options.num_mem_modules
        address_space = make_addr_space(mem_size, num_mem_modules)

        # Attach DRAMs to the memory controller
        drams = []
        for addr_range in address_space:
            dram = Disag_DDR4_2400_4x16()
            dram.range = addr_range
            drams.append(dram)
        mem_ctrl.drams = drams

        # A memory controller's config is done
        # connect it to the memory pool manager
        mem_ctrl.port = mem_pool_manager.mem_side_ports

        # Wrap the memory pool controller with memory sub module 
        mem_sub_module.mem_ctrl = mem_ctrl
        mem_sub_module.memories = drams
        mem_sub_module_list.append(mem_sub_module)

    # Include the memory sub module in the memory module
    memory_module.mem_sub_module_list = mem_sub_module_list

    # Include the memory pool manager in the memory module
    memory_module.membus = mem_pool_manager

    # InterconnectLogic to link the memory module and the interconnect
    # Currently paremeters, including the CID(2), are hard-coded
    last_cpu_cid = options.num_cpu_modules
    interconnect_logic = InterconnectLogicConfig.create_d_logic(
        options, last_cpu_cid, rcv_trans=False, snd_trans=True, logic_latency=options.d_logic_mem_pool_latency,
        num_processing_units = options.num_mem_pool_logic_processing_units
        # options, last_cpu_cid, rcv_trans=False, snd_trans=False
        # options, 2, rcv_trans=False, snd_trans=False
        # options, 2, rcv_trans=False, snd_trans=True
        # Currently no recv translation information
        # options, 2, rcv_trans=True, snd_trans=True
    )

    # Connect the memory module to its interconnect logic
    mem_pool_manager.cpu_side_ports = interconnect_logic.internal_requestor
    # mem_pool_manager.mem_side_ports = interconnect_logic.external_responder
    # mem_pool_manager.d_logic = interconnect_logic
    memory_module.d_logic = interconnect_logic

    return [memory_module]


# DEPRECATED
def config_mem_module(options, mem_modules,mem_mode,cid):
    """
    Configs each memory in disaggregated system, and appends it to the mem_modules list.
    """

    module = MemoryModule(
                mem_mode = mem_mode,
                mem_ranges = [AddrRange(options.d_mem_size)],
                cache_line_size = options.cacheline_size,
                workload = NULL)

    # Create a top-level voltage domain
    module.voltage_domain = VoltageDomain(voltage = options.d_mem_voltage)

    # Create a source clock for the system and set the clock period
    module.clk_domain = SrcClockDomain(clock =  options.d_mem_clock,
                                    voltage_domain = module.voltage_domain)

    # Create a connection between
    # media controller(i.e. one or more mem controllers)
    # and interconnect logic
    d_logic_media_ctrl = NoncoherentXBar()
    d_logic_media_ctrl.frontend_latency = \
        options.d_logic_media_frontend_latency
    d_logic_media_ctrl.forward_latency = options.d_logic_media_forward_latency
    d_logic_media_ctrl.response_latency = \
        options.d_logic_media_response_latency
    mem_cls = ObjectList.mem_list.get(options.mem_type)
    d_logic_media_ctrl.width = mem_cls.device_bus_width
    module.membus = d_logic_media_ctrl

    MemConfig.config_mem(options, module)

    # Connect an interconnect logic
    module.d_logic = InterconnectLogicConfig.create_d_logic(options, cid, rcv_trans = False, snd_trans = False)
    module.membus.cpu_side_ports= module.d_logic.internal_requestor

    mem_modules.append(module)
