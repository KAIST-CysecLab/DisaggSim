from __future__ import print_function
from __future__ import absolute_import

import optparse
import sys
import os
import datetime

import m5
from m5.defines import buildEnv
from m5.objects import *
from m5.params import NULL
from m5.util import addToPath, fatal, warn

addToPath('../')

from ruby import Ruby

from common import Options
from common import Simulation
from common import FileSystemConfig

import DisaggregatedSimulation
import CPUModuleConfig
import MemModuleConfig
import InterconnectConfig
import InterconnectLogicConfig
import Disaggregated_Ruby

# Parse commandline options
parser = optparse.OptionParser()
Options.addCommonOptions(parser)
Options.addSEOptions(parser)
Options.addDisaggregatedOptions(parser)
CPUModuleConfig.define_options(parser)
MemModuleConfig.define_options(parser)
InterconnectLogicConfig.define_options(parser)
Disaggregated_Ruby.define_options(parser)

(options, args) = parser.parse_args()

if args:
    print("Error: script doesn't take any positional arguments")
    sys.exit(1)

# CPU and Memory
# config_xxx_moudle functions add modules to these lists
cpu_modules = []
# mem_modules = []

MemClass = Simulation.setMemClass(options)
(_, mem_mode, FutureClass) = Simulation.setCPUClass(options)

# CPU modules
for i in range(options.num_cpu_modules):
    CPUModuleConfig.config_cpu_module(options, cpu_modules, cid = i)
    FileSystemConfig.config_filesystem(cpu_modules[i], options)

# memory module configuration
# Currently CID is hard-coded:
#  Memory pool manager - Last CPU's CID + 1
#  Memory controller - subCID 0 - (n-1)
mem_modules = MemModuleConfig.create_memory_pool(options, mem_mode)

# For now, it uses CIDs and address mapping information
# that are initialized in the constructor
# CPU's CID: 0 ~ (# of CPUs - 1)
# MemPoolManager's CID: Last CPU's CID + 1
UNIT_MB = (1 << 20)
d_system = DisaggregatedSystem()
d_system.num_cpu_modules = options.num_cpu_modules
d_system.num_mem_ctrls = options.num_mem_ctrls
d_system.size_per_mem_ctrl = options.num_mem_modules * options.size_mem_module * UNIT_MB
d_system.mem_interleave = options.mem_interleave
d_system.interleave_unit = options.interleave_unit
d_system.cpu_modules = cpu_modules
d_system.mem_modules = mem_modules

# Interconnect
# Use Ruby interconnect or not, according to the commandline options
if options.d_ruby:
    all_modules = cpu_modules + mem_modules
    dummy_system = System(is_dummy = True)
    d_system.ruby_dummy_system = dummy_system
    dummy_system.clk_domain = SrcClockDomain(clock = options.d_ruby_clock,
                                        voltage_domain = VoltageDomain())

    (module_sequencers,module_controllers) = \
        Disaggregated_Ruby.create_system(options, False, dummy_system, len(all_modules))

    for index in range(len(all_modules)):
        all_modules[index].d_logic.external_requestor = \
            module_sequencers[index].responder
        all_modules[index].d_logic.external_responder = \
            module_sequencers[index].requestor
else:
    InterconnectConfig.config_interconnect(options,d_system)

# Start disaggregated system simulation
root = Root(full_system = False, d_system = d_system)
DisaggregatedSimulation.d_run(options, root, d_system, FutureClass)

print("gem5 completed %s" %
        datetime.datetime.now().strftime("%b %e %Y %X"))
