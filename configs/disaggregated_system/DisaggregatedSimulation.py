import m5
from m5.defines import buildEnv
from m5.objects import *
from m5.util import *

from common.Simulation import *


# Disaggregated replacement for repeatSwitch
def d_repeatSwitch(cpu_modules, repeat_switch_cpu_lists, maxtick, switch_freq):
    print("starting switch loop")
    while True:

        exit_event = m5.simulate(switch_freq)
        exit_cause = exit_event.getCause()

        if exit_cause != "simulate() limit reached":
            return exit_event
        m5.d_switchCpus(cpu_modules, repeat_switch_cpu_lists)

        tmp_cpu_list = []
        for old_cpu, new_cpu in repeat_switch_cpu_list:
            tmp_cpu_list.append((new_cpu, old_cpu))
        repeat_switch_cpu_list = tmp_cpu_list

        if (maxtick - m5.curTick()) <= switch_freq:
            exit_event = m5.simulate(maxtick - m5.curTick())
            return exit_event


# Disaggregated replacement for run
def d_run(options, root, d_system, cpu_class):
    if options.checkpoint_dir:
        cptdir = options.checkpoint_dir
    elif m5.options.outdir:
        cptdir = m5.options.outdir
    else:
        cptdir = getcwd()

    if options.fast_forward and options.checkpoint_restore != None:
        fatal("Can't specify both --fast-forward and --checkpoint-restore")

    if options.standard_switch and not options.caches:
        fatal("Must specify --caches when using --standard-switch")

    if options.standard_switch and options.repeat_switch:
        fatal("Can't specify both --standard-switch and --repeat-switch")

    if options.repeat_switch and options.take_checkpoints:
        fatal("Can't specify both --repeat-switch and --take-checkpoints")

    cpu_modules = d_system.cpu_modules
    np = options.num_cpus

    switch_cpu_lists = []
    repeat_switch_cpu_lists = []
    switch_cpu_lists_1 = []


    # Configure each cpu module
    for (module_index, module) in enumerate(cpu_modules):
        if options.prog_interval:
            for i in range(np):
                module.cpu[i].progress_interval = options.prog_interval

        if options.maxinsts:
            for i in range(np):
                module.cpu[i].max_insts_any_thread = options.maxinsts

        if cpu_class:
            switch_cpus = [cpu_class(switched_out=True, cpu_id=(i))
                        for i in range(np)]

            for i in range(np):
                if options.fast_forward:
                    module.cpu[i].max_insts_any_thread = \
                        int(options.fast_forward)
                switch_cpus[i].system = module
                switch_cpus[i].workload = module.cpu[i].workload
                switch_cpus[i].clk_domain = module.cpu[i].clk_domain
                switch_cpus[i].progress_interval = \
                    module.cpu[i].progress_interval
                switch_cpus[i].isa = module.cpu[i].isa
                # simulation period
                if options.maxinsts:
                    switch_cpus[i].max_insts_any_thread = options.maxinsts
                # Add checker cpu if selected
                if options.checker:
                    switch_cpus[i].addCheckerCpu()
                if options.bp_type:
                    bpClass = ObjectList.bp_list.get(options.bp_type)
                    switch_cpus[i].branchPred = bpClass()
                if options.indirect_bp_type:
                    IndirectBPClass = ObjectList.indirect_bp_list.get(
                        options.indirect_bp_type)
                    switch_cpus[i].branchPred.indirectBranchPred = \
                        IndirectBPClass()

            # If elastic tracing is enabled attach the elastic trace probe
            # to the switch CPUs
            if options.elastic_trace_en:
                CpuConfig.config_etrace(cpu_class, switch_cpus, options)

            module.switch_cpus = switch_cpus
            switch_cpu_list = [
                (module.cpu[i], switch_cpus[i]) for i in range(np)
            ]

            switch_cpu_lists.append(switch_cpu_list)

        if options.repeat_switch:
            switch_class = getCPUClass(options.cpu_type)[0]
            if switch_class.require_caches() and \
                    not options.caches:
                print("%s: Must be used with caches" % str(switch_class))
                sys.exit(1)
            if not switch_class.support_take_over():
                print("%s: CPU switching not supported" % str(switch_class))
                sys.exit(1)

            repeat_switch_cpus = [switch_class(switched_out=True, \
                                                cpu_id=(i)) for i in range(np)]

            for i in range(np):
                repeat_switch_cpus[i].system = module
                repeat_switch_cpus[i].workload = module.cpu[i].workload
                repeat_switch_cpus[i].clk_domain = module.cpu[i].clk_domain
                repeat_switch_cpus[i].isa = module.cpu[i].isa

                if options.maxinsts:
                    repeat_switch_cpus[i].max_insts_any_thread = \
                        options.maxinsts

                if options.checker:
                    repeat_switch_cpus[i].addCheckerCpu()

            module.repeat_switch_cpus = repeat_switch_cpus

            if cpu_class:
                repeat_switch_cpu_list = [
                    (switch_cpus[i], repeat_switch_cpus[i]) for i in range(np)
                ]
            else:
                repeat_switch_cpu_list = [
                    (module.cpu[i], repeat_switch_cpus[i]) for i in range(np)
                ]

            repeat_switch_cpu_lists.append(repeat_switch_cpu_list)

        if options.standard_switch:
            switch_cpus = [TimingSimpleCPU(switched_out=True, cpu_id=(i))
                        for i in range(np)]
            switch_cpus_1 = [DerivO3CPU(switched_out=True, cpu_id=(i))
                            for i in range(np)]

            for i in range(np):
                switch_cpus[i].system =  module
                switch_cpus_1[i].system =  module
                switch_cpus[i].workload = module.cpu[i].workload
                switch_cpus_1[i].workload = module.cpu[i].workload
                switch_cpus[i].clk_domain = module.cpu[i].clk_domain
                switch_cpus_1[i].clk_domain = module.cpu[i].clk_domain
                switch_cpus[i].isa = module.cpu[i].isa
                switch_cpus_1[i].isa = module.cpu[i].isa

                # if restoring,
                # make atomic cpu simulate only a few instructions
                if options.checkpoint_restore != None:
                    module.cpu[i].max_insts_any_thread = 1
                # Fast forward to specified location if we are not restoring
                elif options.fast_forward:
                    module.cpu[i].max_insts_any_thread = \
                        int(options.fast_forward)
                # Fast forward to a simpoint (warning: time consuming)
                elif options.simpoint:
                    if module.cpu[i].workload[0].simpoint == 0:
                        fatal('simpoint not found')
                    module.cpu[i].max_insts_any_thread = \
                        module.cpu[i].workload[0].simpoint
                # No distance specified, just switch
                else:
                    module.cpu[i].max_insts_any_thread = 1

                # warmup period
                if options.warmup_insts:
                    switch_cpus[i].max_insts_any_thread =  options.warmup_insts

                # simulation period
                if options.maxinsts:
                    switch_cpus_1[i].max_insts_any_thread = options.maxinsts

                # attach the checker cpu if selected
                if options.checker:
                    switch_cpus[i].addCheckerCpu()
                    switch_cpus_1[i].addCheckerCpu()

            module.switch_cpus = switch_cpus
            module.switch_cpus_1 = switch_cpus_1
            switch_cpu_list = [
                (module.cpu[i], switch_cpus[i]) for i in range(np)
            ]
            switch_cpu_list1 = [
                (switch_cpus[i], switch_cpus_1[i]) for i in range(np)
            ]

            switch_cpu_lists.append(switch_cpu_list)
            switch_cpu_lists_1.append(switch_cpu_list1)

            # TODO-Disaggregated
            # Check when time allows
            # set the checkpoint in the cpu before m5.instantiate is called
            if options.take_checkpoints != None and \
                (options.simpoint or options.at_instruction):
                offset = int(options.take_checkpoints)
                # Set an instruction break point
                if options.simpoint:
                    for i in range(np):
                        if module.cpu[i].workload[0].simpoint == 0:
                            fatal(
                                'no simpoint for module.cpu[%d].workload[0]', i
                            )
                        checkpoint_inst = \
                            int(module.cpu[i].workload[0].simpoint) + offset
                        module.cpu[i].max_insts_any_thread = checkpoint_inst
                        # used for output below
                        options.take_checkpoints = checkpoint_inst
                else:
                    options.take_checkpoints = offset
                    # Set all test cpus with the right number of instructions
                    # for the upcoming simulation
                    for i in range(np):
                        module.cpu[i].max_insts_any_thread = offset

    if options.take_simpoint_checkpoints != None:
        simpoints, interval_length = \
            parseSimpointAnalysisFile(options, cpu_modules[0])

    checkpoint_dir = None
    if options.checkpoint_restore:
        cpt_starttick, checkpoint_dir = \
            findCptDir(options, cptdir, cpu_modules[0])



    # Configure root and instantiate all modules
    root.apply_config(options.param)
    m5.instantiate(checkpoint_dir)

    # Initialization is complete.  If we're not in control of simulation
    # (that is, if we're a slave simulator acting as a component in another
    #  'master' simulator) then we're done here.  The other simulator will
    # call simulate() directly. --initialize-only is used to indicate this.
    if options.initialize_only:
        return

    # Handle the max tick settings now that tick frequency was resolved
    # during system instantiation
    # NOTE: the maxtick variable here is in absolute ticks, so it must
    # include any simulated ticks before a checkpoint
    explicit_maxticks = 0
    maxtick_from_abs = m5.MaxTick
    maxtick_from_rel = m5.MaxTick
    maxtick_from_maxtime = m5.MaxTick
    if options.abs_max_tick:
        maxtick_from_abs = options.abs_max_tick
        explicit_maxticks += 1
    if options.rel_max_tick:
        maxtick_from_rel = options.rel_max_tick
        if options.checkpoint_restore:
            # NOTE: this may need to be updated if checkpoints ever store
            # the ticks per simulated second
            maxtick_from_rel += cpt_starttick
            if options.at_instruction or options.simpoint:
                warn("Relative max tick specified with --at-instruction or" \
                     " --simpoint\n      These options don't specify the " \
                     "checkpoint start tick, so assuming\n      you mean " \
                     "absolute max tick")
        explicit_maxticks += 1
    if options.maxtime:
        maxtick_from_maxtime = m5.ticks.fromSeconds(options.maxtime)
        explicit_maxticks += 1
    if explicit_maxticks > 1:
        warn(
            "Specified multiple of --abs-max-tick, --rel-max-tick, --maxtime."\
             " Using least"
        )
    maxtick = min([maxtick_from_abs, maxtick_from_rel, maxtick_from_maxtime])

    if options.checkpoint_restore != None and maxtick < cpt_starttick:
        fatal("Bad maxtick (%d) specified: " \
              "Checkpoint starts starts from tick: %d", maxtick, cpt_starttick)


    # switchCpus for each cpu module
    if options.standard_switch or cpu_class:
        if options.standard_switch:
            print("Switch at instruction count:%s" %
                    str(testsys.cpu[0].max_insts_any_thread))
            exit_event = m5.simulate()
        elif cpu_class and options.fast_forward:
            print("Switch at instruction count:%s" %
                    str(testsys.cpu[0].max_insts_any_thread))
            exit_event = m5.simulate()
        else:
            print("Switch at curTick count:%s" % str(10000))
            exit_event = m5.simulate(10000)
        print("Switched CPUS @ tick %s" % (m5.curTick()))

        m5.d_switchCpus(cpu_modules, switch_cpu_lists)

        if options.standard_switch:
            print("Switch at instruction count:%d" %
                    (testsys.switch_cpus[0].max_insts_any_thread))

            #warmup instruction count may have already been set
            if options.warmup_insts:
                exit_event = m5.simulate()
            else:
                exit_event = m5.simulate(options.standard_switch)
            print("Switching CPUS @ tick %s" % (m5.curTick()))
            print("Simulation ends instruction count:%d" %
                    (testsys.switch_cpus_1[0].max_insts_any_thread))
            m5.d_switchCpus(cpu_modules, switch_cpu_lists_1)

    # If we're taking and restoring checkpoints, use checkpoint_dir
    # option only for finding the checkpoints to restore from.  This
    # lets us test checkpointing by restoring from one set of
    # checkpoints, generating a second set, and then comparing them.
    if (options.take_checkpoints or options.take_simpoint_checkpoints) \
        and options.checkpoint_restore:

        if m5.options.outdir:
            cptdir = m5.options.outdir
        else:
            cptdir = getcwd()

    if options.take_checkpoints != None :
        # Checkpoints being taken via the command line at <when> and at
        # subsequent periods of <period>.  Checkpoint instructions
        # received from the benchmark running are ignored and skipped in
        # favor of command line checkpoint instructions.
        exit_event = scriptCheckpoints(options, maxtick, cptdir)

    # Take SimPoint checkpoints
    elif options.take_simpoint_checkpoints != None:
        takeSimpointCheckpoints(simpoints, interval_length, cptdir)

    # Restore from SimPoint checkpoints
    elif options.restore_simpoint_checkpoint != None:
        restoreSimpointCheckpoint()

    else:
        if options.fast_forward:
            m5.stats.reset()
        print("**** REAL SIMULATION ****")

        # If checkpoints are being taken, then the checkpoint instruction
        # will occur in the benchmark code it self.
        if options.repeat_switch and maxtick > options.repeat_switch:
            exit_event = d_repeatSwitch(cpu_modules, repeat_switch_cpu_lists,
                                      maxtick, options.repeat_switch)
        else:
            exit_event = benchCheckpoints(options, maxtick, cptdir)

    print('Exiting @ tick %i because %s' %
          (m5.curTick(), exit_event.getCause()))
    if options.checkpoint_at_end:
        m5.checkpoint(joinpath(cptdir, "cpt.%d"))

    if exit_event.getCode() != 0:
        print("Simulated exit code not 0! Exit code is", exit_event.getCode())
