from __future__ import print_function
from __future__ import absolute_import

import optparse
import sys
import os

import m5
from m5.defines import buildEnv
from m5.objects import *
from m5.params import NULL
from m5.util import addToPath, fatal, warn

addToPath('../')

from ruby import Ruby

from common import Options
from common import Simulation
from common import CacheConfig
from common import CpuConfig
from common import ObjectList
from common import MemConfig
from common.FileSystemConfig import config_filesystem
from common.Caches import *
from common.cpu2000 import *
import InterconnectLogicConfig

def define_options(parser):
    """Parses commandline options for CPU configuration"""

    parser.add_option(
        "--num_cpu_modules",
        type="int",
        default=2
    )
    parser.add_option(
        "--allocated_d_mem_size",
        type="string",
        default='1024MB'
    )
    parser.add_option(
        "--local_mem_size",
        type="string"
    )
    parser.add_option(
        "--local_mem_type",
        type="string",
        default = 'DDR4_2400_16x4'
    )
    parser.add_option(
        "--num_cpus",
        type="int",
        default = 1
    )

    parser.add_option("--chkpt", default="",
                    help="The checkpoint to load.")

def get_processes(options):
    """Interprets provided options and returns a list of processes"""

    multiprocesses = []
    inputs = []
    outputs = []
    errouts = []
    pargs = []

    workloads = options.cmd.split(';')
    if options.input != "":
        inputs = options.input.split(';')
    if options.output != "":
        outputs = options.output.split(';')
    if options.errout != "":
        errouts = options.errout.split(';')
    if options.options != "":
        pargs = options.options.split(';')

    idx = 0
    for wrkld in workloads:
        process = Process(pid = 100 + idx)
        process.executable = wrkld
        process.cwd = os.getcwd()

        if options.env:
            with open(options.env, 'r') as f:
                process.env = [line.rstrip() for line in f]

        if len(pargs) > idx:
            process.cmd = [wrkld] + pargs[idx].split()
        else:
            process.cmd = [wrkld]

        if len(inputs) > idx:
            process.input = inputs[idx]
        if len(outputs) > idx:
            process.output = outputs[idx]
        if len(errouts) > idx:
            process.errout = errouts[idx]

        multiprocesses.append(process)
        idx += 1

    if options.smt:
        assert(options.cpu_type == "DerivO3CPU")
        return multiprocesses, idx
    else:
        return multiprocesses, 1


def config_cpu_module(options, cpu_modules, cid):
    """
    Configs each CPU in disaggregated system,
    and appends it to the cpu_modules list.
    Note that, CID is assigned to an interconnect logic attached to each CPU,
    not the CPU itself.
    """

    multiprocesses = []
    numThreads = 1

    ##  Get the workload from the commandline options ##

    # When it's the benchmark mode
    if options.bench:
        print("please use script from benchmark_test directory")
        sys.exit(1)
        # apps = options.bench.split("-")
        # if len(apps) != options.num_cpus:
        #     print("number of benchmarks not equal to set num_cpus!")
        #     sys.exit(1)

        # # for app in apps:
        # #     try:
        # #         if buildEnv['TARGET_ISA'] == 'arm':
        # #             exec("workload = %s('arm_%s', 'linux', '%s')" % (
        # #                     app, options.arm_iset, options.spec_input))
        # #         else:
        # #             exec(
        # #                 "workload = %s(buildEnv['TARGET_ISA', 'linux', '%s')" %
        # #                 (app, options.spec_input))
        # #         multiprocesses.append(workload.makeProcess())
        # #     except:
        # #         print("Unable to find workload for %s: %s" %
        # #             (buildEnv['TARGET_ISA'], app),
        # #             file=sys.stderr)
        # #         sys.exit(1)

        # for app in apps:
        #     multiprocesses.append(SPEC2006Bench.create_bench_proc(app, options.chkpt))
    # Or, when it's given a specific workload
    elif options.cmd:
        multiprocesses, numThreads = get_processes(options)
    # Quit if there's no workload
    else:
        print("No workload specified. Exiting!\n", file=sys.stderr)
        sys.exit(1)

    # Set the basic CPU options
    (CPUClass, test_mem_mode, FutureClass) = Simulation.setCPUClass(options)
    CPUClass.numThreads = numThreads

    # Check -- do not allow SMT with multiple CPUs
    if options.smt and options.num_cpus > 1:
        fatal("You cannot use SMT with multiple CPUs!")

    # Create the CPUs
    np = options.num_cpus
    module = CPUModule(cpu = [CPUClass(cpu_id=i) for i in range(np)],
                    mem_mode = test_mem_mode,
                    cache_line_size = options.cacheline_size,
                    workload = NULL)

    # Multi-threads mode?
    if numThreads > 1:
        module.multi_thread = True

    # Create a top-level voltage domain
    module.voltage_domain = VoltageDomain(voltage = options.sys_voltage)

    # Create a source clock for the system and set the clock period
    module.clk_domain = SrcClockDomain(clock =  options.sys_clock,
                                    voltage_domain = module.voltage_domain)

    # Create a CPU voltage domain
    module.cpu_voltage_domain = VoltageDomain()

    # Create a separate clock domain for the CPUs
    module.cpu_clk_domain = SrcClockDomain(clock = options.cpu_clock,
                                        voltage_domain =
                                        module.cpu_voltage_domain)

    # If elastic tracing is enabled,
    # then configure the cpu and attach the elastic
    # trace probe
    if options.elastic_trace_en:
        CpuConfig.config_etrace(CPUClass, module.cpu, options)

    # All cpus belong to a common cpu_clk_domain, therefore running at a common
    # frequency.
    for cpu in module.cpu:
        cpu.clk_domain = module.cpu_clk_domain

    if ObjectList.is_kvm_cpu(CPUClass) or ObjectList.is_kvm_cpu(FutureClass):
        if buildEnv['TARGET_ISA'] == 'x86':
            module.kvm_vm = KvmVM()
            for process in multiprocesses:
                process.useArchPT = True
                process.kvmInSE = True
        else:
            fatal("KvmCPU can only be used in SE mode with x86")

    # Sanity check
    if options.simpoint_profile:
        if not ObjectList.is_noncaching_cpu(CPUClass):
            fatal("SimPoint/BPProbe should be done with an atomic cpu")
        if np > 1:
            fatal("SimPoint generation not supported with more than one CPUs")

    # CPU-wisely set various options
    for i in range(np):
        if options.smt:
            module.cpu[i].workload = multiprocesses
        elif len(multiprocesses) == 1:
            module.cpu[i].workload = multiprocesses[0]
        else:
            module.cpu[i].workload = multiprocesses[i]

        if options.simpoint_profile:
            module.cpu[i].addSimPointProbe(options.simpoint_interval)

        if options.checker:
            module.cpu[i].addCheckerCpu()

        if options.bp_type:
            bpClass = ObjectList.bp_list.get(options.bp_type)
            module.cpu[i].branchPred = bpClass()

        if options.indirect_bp_type:
            indirectBPClass = \
                ObjectList.indirect_bp_list.get(options.indirect_bp_type)
            module.cpu[i].branchPred.indirectBranchPred = indirectBPClass()

        module.cpu[i].createThreads()


    ## Setup each CPU's local memory ##

    # For convenience, replace mem_size and mem_type with local_*
    # but backup for later use (in case other components rely on it)
    mem_size_backup = options.mem_size
    mem_type_backup = options.mem_type

    # Local memory (which each CPU *may* have) parameters
    options.mem_size = options.local_mem_size
    options.mem_type = options.local_mem_type

    MemClass = Simulation.setMemClass(options)

    # Create XBar (which will be used for connecting interconnect logic)
    module.membus = SystemXBar()
    module.system_port = module.membus.cpu_side_ports

    # Setup CPU caches
    CacheConfig.config_cache(options, module)

    # Configure file system
    config_filesystem(module, options)

    if options.mem_size == None or options.mem_size == '0MB':
        # Create a dummy memory (representing remote memory)
        # => full disaggregation
        module.mem_ranges = [AddrRange(options.allocated_d_mem_size)]
        module.forward_mem = ForwardMemory()
        module.forward_mem.range = module.mem_ranges[0]
    else:
        # Create a local memory if required
        module.mem_ranges = [AddrRange(options.mem_size)]
        MemConfig.config_mem(options, module)

        module.mem_ranges = [AddrRange(options.mem_size, options.allocated_d_mem_size)]
        module.forward_mem = ForwardMemory()
        module.forward_mem.range = module.mem_ranges[0]

    ## Setup each CPU's interconnect logic ##

    # Create an interconnect logic
    # which translate packets' address into disaggregated system's CID address
    # by default
    module.d_logic = InterconnectLogicConfig.create_d_logic(
        options,
        cid,
        rcv_trans = True,
        snd_trans = True
    )

    # Connecting the CPU and the interconnect logic via XBar
    module.membus.mem_side_ports = module.d_logic.internal_responder
    module.membus.cpu_side_ports = module.d_logic.internal_requestor

    # Restore mem_size and mem_type (in case other components rely on it)
    options.mem_size = mem_size_backup
    options.mem_type = mem_type_backup

    ## Other things ##

    # External GDB connection
    if options.wait_gdb:
        for cpu in module.cpu:
            cpu.wait_for_remote_gdb = False

    # Append this CPU to the cpu_modules list
    cpu_modules.append(module)


    # module.membus.mem_side_ports = module.forward_mem.internal_responder
    # module.membus.cpu_side_ports = module.forward_mem.internal_requestor

    # Create a disaggregated interconnect logic
    # module.d_logic = InterconnectLogicConfig.create_d_logic(options, cid)

    # # Connect the disaggregated interconnect logic with the dummy memory
    # module.forward_mem.external_requestor = module.d_logic.internal_responder
    # module.forward_mem.external_responder = module.d_logic.internal_requestor
