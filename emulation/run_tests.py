import os
from setup import run_experiment
from mininet.log import setLogLevel
setLogLevel('output')

experiments_file = 'experiments.log'
experiment_id = 0
experiment_dirs = {}

if os.geteuid() != 0:
    exit("This script needs to run in root privileges!")

def print_divider(thick=True):
    if thick:
        print('======================================')
    else:
        print('--------------------------------------')

def run(**kwargs):
    global experiment_id
    global experiment_dirs

    # Print a simple experiment header
    print_divider()
    print(f'Experiment: {experiment_id}')
    print_divider(thick=False)

    # Print all arguments used in this experiment
    for entry in kwargs.items():
        print("{}:{}".format(entry[0], entry[1]))
    print_divider(thick=False)

    # Run the experiment.
    log_dir = run_experiment(**kwargs)
    experiment_dirs[experiment_id] = log_dir

    # Print a small footer that informs wich experiment has ended.
    print_divider(thick=False)
    print(f'Experiment {experiment_id} has ended')
    print_divider()
    print()
    experiment_id += 1

def print_and_save_directories():
    global experiment_dirs
    print_divider()
    with open(f'../evaluation/logs/{experiments_file}', 'w') as file:
        for entry in experiment_dirs.items():
            print(f"Experiment {entry[0]} is stored in {entry[1]}")
            file.write(f'{entry[0]}: {entry[1]}\n')

'''
======================================
Shared variables
======================================
'''
bandwidth    = 100     # Mb/s
latency      = 20      # ms
n_subnets    = 1     
n_clients    = 1     
seg_dur      = 1       # s
mc_s_sleep   = [18]      # s
extra_buffer = 0       # s,
c_s_sleep    = [x + seg_dur for x in mc_s_sleep] # s
videos       = ["avatar"]

'''
======================================
No loss, no fec, 20ms latency, 5 proxies, no multicast
======================================
'''
run(
    bandwidth   = bandwidth     ,  # Mb/s
    latency     = latency       ,  # ms
    loss        = 0             ,  # %
    fec         = 0             ,  # 0: Compact No Code FEC, 1: Raptor FEC
    n_subnets   = n_subnets     ,
    n_clients   = n_clients     ,
    seg_dur     = seg_dur       ,  # s
    rep_ids     = ['3']         ,  # Quality levels which we will multicast, unused in this case, so doesn't matter
    videos      = videos        ,  # Videos to be used in this experiment
    enable_auto_clients=True,
    multicast_sender_sleep_time = mc_s_sleep, # s
    clients_sleep_time=c_s_sleep, # s
    wait_for_clients_to_finish=True,
    wait_for_multicast_to_finish=True,
    enable_tli=False, # TLI is disabled
    disable_multicast=True) # Multicast is disabled

'''
======================================
Other test cases
======================================
'''
fecs = [0, 1]
#losses = [0, 0.5, 1, 2, 5, 10, 15, 17, 18, 20, 50, 75, 90, 95, 100]
losses = [0, 0.5, 1, 2, 5, 10, 15, 20]
combinations= [
    #[['1', '3'], ['.mp4', '.mp4.IPB.h264']],
    #[['1', '3', '4'], ['.mp4', '.mp4.IPB.h264', '.mp4.IP.h264']],
    #[['1', '3', '5'], ['.mp4', '.mp4.IPB.h264', '.mp4.IPB.h264']],
    #[['5'], ['.mp4']],
    [['3'], ['.mp4']]
]
for combination in combinations:
    for loss in losses:
        for fec in fecs:
            run(
                bandwidth      = bandwidth          ,  # Mb/s
                latency        = latency            ,  # ms
                loss           = loss               ,  # %
                fec            = fec                ,  # 0: Compact No Code FEC, 1: Raptor FEC
                n_subnets      = n_subnets          ,
                n_clients      = n_clients          ,
                seg_dur        = seg_dur            ,  # s
                rep_ids        = combination[0]     ,  # Quality levels which we will multicast
                rep_extensions = combination[1]     ,  # The file corresponding to the quality level that we will multicast
                videos         = videos             ,  # Videos to be used in this experiment
                enable_auto_clients=True,
                multicast_sender_sleep_time = mc_s_sleep, # s
                clients_sleep_time=c_s_sleep, # s
                wait_for_clients_to_finish=True,
                enable_tli=False, # TLI is disabled
                wait_for_multicast_to_finish=True)

print_and_save_directories()
