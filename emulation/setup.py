#!/usr/bin/python

from mininet.clean import cleanup
from mininet.topo import Topo
from mininet.net import Mininet
from mininet.node import Node
from mininet.nodelib import NAT
from mininet.log import setLogLevel, info
from mininet.cli import CLI
import time
import os
import subprocess
import re

net = None

class LinuxRouter(Node):
    # A Node with IP forwarding enabled.

    def config(self, **params):
        super(LinuxRouter, self).config(**params)
        # Enable forwarding on the router
        self.cmd('sysctl -w net.ipv4.ip_forward=1')
        self.cmd('sysctl -w net.ipv4.icmp_echo_ignore_broadcasts=0')
        self.cmd('sysctl -w net.ipv4.conf.r0-eth1.force_igmp_version=2')
        self.cmd('sysctl -w net.ipv4.conf.r0-eth1.rp_filter=0')
        n_subnets = params['n_subnets']
        for i in range(2, n_subnets + 2):
            self.cmd(f'sysctl -w net.ipv4.conf.r0-eth{i}.force_igmp_version=2')
            self.cmd(f'sysctl -w net.ipv4.conf.r0-eth{i}.rp_filter=0')
        self.cmd('smcrouted -l debug -I smcroute-r0')
        self.cmd('sleep 1')
        for i in range(1, n_subnets + 2):
            l = [f'r0-eth{j}' for j in range(1, n_subnets + 2) if j != i]
            self.cmd(f'smcroutectl -I smcroute-r0 add r0-eth{i} 239.0.0.{i} {" ".join(l)}')

    def terminate(self):
        self.cmd('sysctl -w net.ipv4.ip_forward=0')
        self.cmd('smcroutectl -I smcroute-r0 kill')
        super(LinuxRouter, self).terminate()

class EdgeNode(Node):
    # A Node with multicast stuff.

    def config(self, **params):
        super(EdgeNode, self).config(**params)
        intfName = self.intfNames()[0]
        self.cmd('sysctl net.ipv4.icmp_echo_ignore_broadcasts=0')
        self.cmd(f'route add -net 224.0.0.0 netmask 240.0.0.0 dev {intfName}')
        self.cmd(f'smcrouted -l debug -I smcroute-{self.name}')
        self.cmd('sleep 1')
        self.cmd(f'smcroutectl -I smcroute-{self.name} join {intfName} 239.0.0.1')

    def terminate(self):
        self.cmd(f'smcroutectl -I smcroute-{self.name} kill')
        super(EdgeNode, self).terminate()

class NetworkTopo(Topo):
    # A LinuxRouter connecting three IP subnets

    n_subnets = 1
    n_clients = 1

    def __init__(self, bandwidth=15, latency=0, loss=0.0, n_subnets=1, n_clients=1):
        Topo.__init__( self )

        self.n_subnets = n_subnets
        self.n_clients = n_clients

        # This router is put between the server and all proxies
        router = self.addNode('r0', cls=LinuxRouter, ip='192.168.1.1/24', n_subnets=n_subnets)

        # The server which hosts our files and broadcasts the streams over multicast.
        server = self.addHost('server',
                              cls=EdgeNode,
                              ip='192.168.1.100/24',
                              defaultRoute='via 192.168.1.1')

        # Switch used for multicast connection with the server
        switch = self.addSwitch('switch_1')
        self.addLink(switch, router, intfName2='r0-eth1', params2={'ip': '192.168.1.1/24'})
        self.addLink(server, switch)

        # Switch used for unicast connection with the server
        switch = self.addSwitch('switch_101')
        self.addLink(server, switch, params1={'ip':f'12.0.0.0/24'})

        # Switch used by all the proxies to connect to the server over unicast
        switch_unicast = self.addSwitch('switch_102')
        self.addLink(switch, switch_unicast)
        # Topo: server <--> switch_101 <--> switch_102 <--> (proxies)


        # Topo: server <--> switch_101 <--> nat
        # And: nat <--> switch_101 <--> switch_102 <--> (proxies)

        # Switch used by all the proxies to connect to the server over multicast
        switch_multicast = self.addSwitch('switch_103')
        self.addLink(switch_multicast, router, intfName2=f'r0-eth2')
        # Topo: server <--> switch_1 <--> r0 <--> switch_103 <--> (proxies)

        for i in range(1, n_subnets + 1):
            # Each subnet has its own switch, that is connected to switch_102 (UC) and switch_103 (MC)
            switch = self.addSwitch(f'switch_{i + 1}')
            proxy = self.addHost(f'proxy_{i}',
                                 cls=EdgeNode,
                                 ip=f'11.0.{i}.100/24',
                                 defaultRoute=f'via 11.0.{i}.1')
            # Topo: server <--> switch_1 <--> r0 <--> switch_103 <--> switch_{i + 1} <--> proxy_{i}
            self.addLink(switch_multicast, proxy, params2={'ip': f'11.0.0.{i}/24'})
            self.addLink(switch, proxy, params2={'ip': f'10.0.{i}.100/24'})
            # Topo: server <--> switch_101 <--> switch_102 <--> switch_{i + 1} <--> proxy_{i}
            self.addLink(switch_unicast, proxy, params2={'ip': f'12.0.0.{i}/24'})
            
            for j in range(1, n_clients + 1):
                client = self.addHost(f'client_{i}_{j}',
                                      cls=EdgeNode,
                                      ip=f'10.0.{i}.{100 + j}/24',
                                      defaultRoute=f'via 10.0.{i}.1')
                # Topo: switch_{i + 1} <--> client_{i}_{j}
                self.addLink(client, switch)

def create_directory(folder_path):
    if not os.path.exists(folder_path):
        try:
            os.mkdir(folder_path)
        except OSError as error:
            print(f"Failed to create directory '{folder_path}': {error}")

def check_last_line(file_path, expected_line):
    try:
        with open(file_path, 'r') as file:
            lines = file.readlines()
            if lines and lines[-1].strip() == expected_line:
                return True
            return False
    except FileNotFoundError:
        print(f'{file_path} not found')
        return True # Return True so the program will not be stuck.

def alphanumeric_key(value):
    return [int(s) if s.isdigit() else s for s in re.split('([0-9]+)', value)]

def get_video_info_map(videos=None, rep_ids=None, rep_extensions=None, multicast_sender_sleep_times=None, clients_sleep_times=None):
    videos_to_watch_map = {}

    if videos is None:
        videos = []
    if rep_ids is None:
        rep_ids = []
    if rep_extensions is None:
        rep_extensions = []
    if multicast_sender_sleep_times is None:
        multicast_sender_sleep_times = []
    if clients_sleep_times is None:
        clients_sleep_times = []

    for i in range(len(videos)):
        video = videos[i] if i < len(videos) else videos[0] if len(videos) > 0 else 'alpha'
        rep_id = rep_ids[i] if i < len(rep_ids) else rep_ids[0] if len(rep_ids) > 0 else '1'
        rep_extension = rep_extensions[i] if i < len(rep_extensions) else rep_extensions[0] if len(rep_extensions) > 0 else '.mp4'
        client_sleep_time = clients_sleep_times[i] if i < len(clients_sleep_times) else clients_sleep_times[0] if len(clients_sleep_times) > 0 else 14
        multicast_sender_sleep_time = multicast_sender_sleep_times[i] if i < len(multicast_sender_sleep_times) else multicast_sender_sleep_times[0] if len(multicast_sender_sleep_times) > 0 else 10

        # Force the video to be a list
        if not isinstance(video, list):
            video = [video]

        # For each video in the list
        for j in range(len(video)):
            v = video[j]
            r = (rep_id[j] if isinstance(rep_id, list) and j < len(rep_id) else rep_id[0]) if isinstance(rep_id, list) else rep_id or '1'
            e = (rep_extension[j] if isinstance(rep_extension, list) and j < len(rep_extension) else rep_extension[0]) if isinstance(rep_extension, list) else rep_extension or '.mp4'
            c = (client_sleep_time[j] if isinstance(client_sleep_time, list) and j < len(client_sleep_time) else client_sleep_time[0]) if isinstance(client_sleep_time, list) else client_sleep_time or 14
            m = (multicast_sender_sleep_time[j] if isinstance(multicast_sender_sleep_time, list) and j < len(multicast_sender_sleep_time) else multicast_sender_sleep_time[0]) if isinstance(multicast_sender_sleep_time, list) else multicast_sender_sleep_time or 10

            # Check if the video is already in the map
            if v not in videos_to_watch_map:
                videos_to_watch_map[v] = {}

            # Check if the rep_id is already in the map
            if r not in videos_to_watch_map[v]:
                videos_to_watch_map[v][r] = {}

            # Check if the rep_extension is already in the map
            if e not in videos_to_watch_map[v][r]:
                videos_to_watch_map[v][r][e] = {}

            # Check if client_sleep_time is already in the map, if so then take the minimum
            if 'client_sleep_time' not in videos_to_watch_map[v][r][e] or videos_to_watch_map[v][r][e]['client_sleep_time'] > c:
                videos_to_watch_map[v][r][e]['client_sleep_time'] = c

            # Check if multicast_sender_sleep_time is already in the map, if so then take the minimum
            if 'multicast_sender_sleep_time' not in videos_to_watch_map[v][r][e] or videos_to_watch_map[v][r][e]['multicast_sender_sleep_time'] > m:
                videos_to_watch_map[v][r][e]['multicast_sender_sleep_time'] = m
    
    # Sort the map by key
    sorted_videos_to_watch_map = {}
    for key in sorted(videos_to_watch_map.keys(), key=alphanumeric_key):
        sorted_videos_to_watch_map[key] = videos_to_watch_map[key]

    return sorted_videos_to_watch_map

def run_experiment(
        bandwidth = 0,latency = 0, loss = 0.0, fec = 0,
        n_subnets = 1, n_clients = 1,
        seg_dur = 1, rep_ids=None, rep_extensions=None, videos=None,
        multicast_sender_sleep_time=18, clients_sleep_time=0,
        enable_CLI=False, enable_auto_clients=False,
        wait_for_multicast_to_finish=False, 
        wait_for_clients_to_finish=False,
        sleep_before_exit = 1,
        amount_to_multicast=-1, # -1 means all
        low_latency=False,
        live_latency=8.1,
        chunk_count=1,
        enable_tli=False,
        start_await_flag=None,
        stop_flag=None,
        show_clients=None,
        disable_multicast=False):
    global net

    if videos is None:
        videos = []
    if rep_ids is None:
        rep_ids = ['1']
    if rep_extensions is None:
        rep_extensions = ['.mp4' for _ in rep_ids]

    def nested_list_to_flat_list(l):
        result = []
        for item in l:
            if isinstance(item, list):
                result.extend(nested_list_to_flat_list(item))
            else:
                result.append(item)
        return result

    def nested_list_to_flat_string(l):
        return ','.join([str(x) for x in nested_list_to_flat_list(l)])

    try:
        video_dict = get_video_info_map(videos, rep_ids, rep_extensions, multicast_sender_sleep_time, clients_sleep_time)
        # Convert the dict to arrays
        videos_to_watch_array = []
        videos_to_multicast_array = []
        rep_id_array = []
        rep_extension_array = []
        client_sleep_time_array = []
        multicast_sender_sleep_time_array = []

        for video, rep_ids in video_dict.items():
            for rep_id, rep_extensions in rep_ids.items():
                for rep_extension, sleep_times in rep_extensions.items():
                    videos_to_watch_array.append(video)
                    rep_id_array.append(rep_id)
                    rep_extension_array.append(rep_extension)
                    client_sleep_time_array.append(sleep_times['client_sleep_time'])
                    multicast_sender_sleep_time_array.append(sleep_times['multicast_sender_sleep_time'])

        if amount_to_multicast < 0:
            amount_to_multicast = len(videos_to_watch_array)
        videos_to_multicast_array = []
        # Add the 'amount_to_multicast' first videos to the multicast array
        for i in range(min(amount_to_multicast, len(videos_to_watch_array))):
            videos_to_multicast_array.append(videos_to_watch_array[i])

        print("Cleaning MiniNet")
        cleanup()

        print("Creating topology")
        topo = NetworkTopo(bandwidth, latency, loss, n_subnets, n_clients)
        net = Mininet(topo=topo)
        net.start()

        info('*** Routing Table on Router:\n')
        info(net['r0'].cmd('route'))

        print("Start network shaping")

        # Shape multicast bandwidth
        switch = net.get('switch_1')
        switch.cmd(f'/bin/bash shape_bandwidth.sh switch_1-eth1 {bandwidth}mbit {latency}ms {loss}% 1')
        # Shape unicast bandwidth
        #switch = net.get('switch_101')
        #switch.cmd(f'/bin/bash shape_bandwidth.sh switch_101-eth2 {bandwidth}mbit {2 * latency}ms {loss}% 1')
        time.sleep(1)

        if start_await_flag is not None and stop_flag is not None:
            while not (start_await_flag.is_set() or stop_flag.is_set()):
                time.sleep(0.5)

        print("Starting server HTTP")
        server = net.get('server')
        server.cmd(f'/bin/bash server_interface.sh &')
        server.cmd(f'/bin/bash server_http.sh -m {nested_list_to_flat_string(client_sleep_time_array)} -n {0} -v {nested_list_to_flat_string(videos_to_watch_array)} &')

        print("Starting proxies")
        tli_enabled = 1 if enable_tli else 0
        for i in range(1, n_subnets + 1):
            proxy = net.get(f'proxy_{i}')
            proxy.cmd(f'/bin/bash proxy_interface.sh {i} &')
            proxy.cmd(f'/bin/bash proxy_static.sh -i {i} -q {nested_list_to_flat_string(rep_id_array)} -s {seg_dur} -v {nested_list_to_flat_string(videos_to_watch_array)} &')
            proxy.cmd(f'/bin/bash proxy_http.sh {i} http://12.0.0.0:8000 {fec} {tli_enabled} {seg_dur} {chunk_count} &')
            proxy.cmd(f'/bin/bash proxy_multicast.sh {i} http://12.0.0.0:8000/partial &')
            if enable_tli and i == 1: # Only the first subnet may use TLI
                proxy.cmd(f'/bin/bash proxy_nalu.sh {i} 97 &') # TODO: video length is hard coded to 97

        print("Starting server multicast")
        mc_disabled = 1 if disable_multicast else 0
        server.cmd(f'/bin/bash server_multicast.sh -d {mc_disabled} -q {nested_list_to_flat_string(rep_id_array)} -e {nested_list_to_flat_string(rep_extension_array)} -s {seg_dur} -t {nested_list_to_flat_string(multicast_sender_sleep_time_array)} -f {fec} -v {nested_list_to_flat_string(videos_to_multicast_array)} -l {chunk_count} -r {bandwidth} &')

        if enable_auto_clients and len(videos) > 0:
            if show_clients is None:
                show_clients = [False for _ in range(n_subnets)]

            low_latency_text = "true" if low_latency else "false"
            print("Starting clients")
            client_count = 0
            for i in range(1, n_subnets + 1):
                csts = clients_sleep_time
                vids = videos
                show_client = show_clients
                print(f"Starting clients for subnet {i}")
                # If it is a nested list, then take the correct list
                index = i if isinstance(csts, list) and len(csts) >= i else 1
                if isinstance(csts, list) and csts[index - 1] is not None and isinstance(csts[index - 1], list):
                    csts = csts[index - 1]
                print(f"Client sleep times: {csts}")
                index = i if isinstance(vids, list) and len(vids) >= i else 1
                if isinstance(vids, list) and vids[index - 1] is not None and isinstance(vids[index - 1], list):
                    vids = vids[index - 1]
                print(f"Videos to watch: {vids}")
                index = i if isinstance(show_client, list) and len(show_client) >= i else 1
                if isinstance(show_client, list) and show_client[index - 1] is not None and isinstance(show_client[index - 1], list):
                    show_client = show_client[index - 1]
                # Start clients
                for j in range(1, n_clients + 1):
                    cst = csts[(j - 1) % len(csts)] if isinstance(csts, list) else csts
                    vid = vids[(j - 1) % len(vids)] if isinstance(vids, list) else vids
                    sc = show_client[(j - 1) % len(show_client)] if isinstance(show_client, list) else show_client
                    h = "False" if sc else "True"
                    client = net.get(f'client_{i}_{j}')
                    force_fake_clients = 1 if j > 2 or i > 2 else 0
                    client.cmd(f'/bin/bash client_run.sh http://10.0.{i}.100:33333?seg_dur={seg_dur}\&video={vid}\&live=true\&low_latency={low_latency_text}\&chunk_count={chunk_count}\&live_latency={live_latency} client_{i}_{j} {cst} {h} {tli_enabled} {force_fake_clients} &>> client_{i}_{j}.log &')

        if enable_CLI:
            CLI(net)
        else:
            print("Running emulation...")

        if wait_for_multicast_to_finish and len(videos) > 0:
            time.sleep(1)
            print("Waiting for multicast to finish transmitting...")
            while not check_last_line('../server/server_multicast.log', "All videos sent over multicast, terminating..."):
                time.sleep(2)

        if wait_for_clients_to_finish and enable_auto_clients:
            time.sleep(1)
            for i in range(1, n_subnets + 1):
                for j in range(1, n_clients + 1):
                    client_name = f'client_{i}_{j}'
                    print(f"Waiting for {client_name} to finish transmitting...")
                    while not check_last_line(f'./{client_name}.log', "# Done"):
                        time.sleep(2)

        if sleep_before_exit > 0:
            time.sleep(sleep_before_exit)

        if stop_flag is not None:
            while not stop_flag.is_set():
                time.sleep(0.5)
        
        print("Stopping emulation")

        # r0.cmd('smcroutectl -I smcroute-r0 kill')
        net.stop()
    
    except Exception as e:
        print(e)
        # Cleanup the network
        if net is not None:
            try:
                net.stop()
            except Exception as e:
                print("Failed to stop the network")
    finally:
        net = None # Unset the net variable, so no other functions can use it
        log_dir = str(int(time.time()))
        print(f"Removing caches and saving to evaluation: {log_dir}")
        create_directory(f'../evaluation/logs')
        create_directory(f'../evaluation/logs/{log_dir}')
        for i in range(1, n_subnets + 1):
            process = subprocess.Popen(['/bin/bash', 'proxy_clear.sh', f'{i}', log_dir])
            process.wait()
            if enable_auto_clients:
                for j in range(1, n_clients + 1):
                    process = subprocess.Popen(['/bin/bash', 'client_clear.sh', f'{i}_{j}', log_dir])
                    process.wait()
        
        process = subprocess.Popen(['/bin/bash', 'server_clear.sh', log_dir])
        process.wait()

        # Write data to the file
        with open(f'../evaluation/logs/{log_dir}/info.log', "w") as file:
            file.write(f"bandwidth: {bandwidth}\n")
            file.write(f"latency: {latency}\n")
            file.write(f"loss: {loss}\n")
            file.write(f"n_subnets: {n_subnets}\n")
            file.write(f"n_clients: {n_clients}\n")
            file.write(f"seg_dur: {seg_dur}\n")
            file.write(f"rep_ids: {rep_ids}\n")
            file.write(f"rep_extensions: {rep_extensions}\n")
            file.write(f"videos: {videos}\n")
            file.write(f"multicast_sender_sleep_time: {multicast_sender_sleep_time}\n")
            file.write(f"clients_sleep_time: {clients_sleep_time}\n")
            file.write(f"enable_auto_clients: {enable_auto_clients}\n")
            file.write(f"wait_for_multicast_to_finish: {wait_for_multicast_to_finish}\n")
            file.write(f"wait_for_clients_to_finish: {wait_for_clients_to_finish}\n")
            file.write(f"sleep_before_exit: {sleep_before_exit}\n")
            file.write(f"disable_multicast: {disable_multicast}\n")
            file.write(f"fec: {fec}\n")
            file.write(f"dir: {log_dir}\n")
        return log_dir

def shape_bandwidth(bandwidth, latency, loss, node_name='switch_1', interface_name='switch_1-eth1'):
    global net
    if net is None:
        return
    node = net.get(node_name)
    node.cmd(f'/bin/bash shape_bandwidth.sh {interface_name} {bandwidth}mbit {latency}ms {loss}% 1 &')
    # print(f"Shaping bandwidth on {node_name} with {bandwidth}mbit, {latency}ms, {loss}%")


if __name__ == '__main__':
    if os.geteuid() != 0:
        exit("This script needs to run in root privileges!")

    setLogLevel('info')
    '''
    run_experiment(
        bandwidth      = 100   ,  # Mb/s
        latency        = 0     ,  # ms
        loss           = 0.0   ,  # %
        fec            = 0     ,  # 0: Compact No Code FEC, 1: Raptor FEC
        n_subnets      = 1     ,
        n_clients      = 1     ,
        seg_dur        = 4     ,  # s
        rep_ids        = ['5'],  # Quality levels which we will multicast
        rep_extensions = ['.f4s'],  # Quality levels which we will multicast
        videos         = [['beta_high_1']],
        enable_auto_clients=True,
        enable_CLI=True,
        multicast_sender_sleep_time = [['10']],
        clients_sleep_time=[[14]], # Multicast sender sleep time + segment duration
        wait_for_clients_to_finish=False,
        wait_for_multicast_to_finish=False,
        amount_to_multicast=1,
        low_latency=True,
        enable_tli=True,
        disable_multicast=False,
        sleep_before_exit=5)

    run_experiment(
        bandwidth      = 100   ,  # Mb/s
        latency        = 0     ,  # ms
        loss           = 1.0   ,  # %
        fec            = 0     ,  # 0: Compact No Code FEC, 1: Raptor FEC
        n_subnets      = 1     ,
        n_clients      = 1     ,
        seg_dur        = 4     ,  # s
        low_latency    = True,
        chunk_count    = 1,
        rep_ids        = ['5'],  # Quality levels which we will multicast
        rep_extensions = ['.m4s'],  # Quality levels which we will multicast
        videos         = [['avatarcmaf8_1', 'avatarcmaf8_2', 'avatarcmaf8_3', 'avatarcmaf8_4', 'avatarcmaf8_5']],
        enable_auto_clients=True,
        enable_CLI=True,
        multicast_sender_sleep_time = [['10', '11', '12', '13', '14']],
        clients_sleep_time=[[11, 12, 13, 14, 15]], # Multicast sender sleep time + segment duration
        wait_for_clients_to_finish=False,
        wait_for_multicast_to_finish=False,
        amount_to_multicast=1,
        enable_tli=True,
        disable_multicast=False,
        sleep_before_exit=5)

    
    '''
    run_experiment(
        bandwidth      = 100   ,  # Mb/s
        latency        = 0     ,  # ms
        loss           = 0.0   ,  # %
        fec            = 0     ,  # 0: Compact No Code FEC, 1: Raptor FEC
        n_subnets      = 1     ,
        n_clients      = 1     ,
        seg_dur        = 4     ,  # s
        enable_tli     = False,
        low_latency    = False,
        live_latency   = 2.0,
        chunk_count    = 1,
        rep_ids        = ['5'],  # Quality levels which we will multicast
        rep_extensions = ['.m4s'],  # Quality levels which we will multicast
        videos         = [['avatarcmaf8_1']],
        enable_auto_clients=True,
        show_clients    = [[True]],
        enable_CLI=True,
        multicast_sender_sleep_time = [['5']],
        clients_sleep_time=[[6.0]], # Multicast sender sleep time + segment duration
        wait_for_clients_to_finish=False,
        wait_for_multicast_to_finish=False,
        amount_to_multicast=1,
        disable_multicast=False,
        sleep_before_exit=5)
    '''

    run_experiment(
        bandwidth      = 100   ,  # Mb/s
        latency        = 0     ,  # ms
        loss           = 1.0   ,  # %
        fec            = 0     ,  # 0: Compact No Code FEC, 1: Raptor FEC
        n_subnets      = 1     ,
        n_clients      = 1     ,
        seg_dur        = 1     ,  # s
        low_latency    = True,
        chunk_count    = 1,
        rep_ids        = ['5'],  # Quality levels which we will multicast
        rep_extensions = ['.m4s'],  # Quality levels which we will multicast
        videos         = [['avatarcmaf1_1', 'avatarcmaf1_2', 'avatarcmaf1_3', 'avatarcmaf1_4', 'avatarcmaf1_5']],
        enable_auto_clients=True,
        enable_CLI=True,
        multicast_sender_sleep_time = [['10', '11', '12', '13', '14']],
        clients_sleep_time=[[11, 12, 13, 14, 15]], # Multicast sender sleep time + segment duration
        wait_for_clients_to_finish=False,
        wait_for_multicast_to_finish=False,
        amount_to_multicast=1,
        enable_tli=True,
        disable_multicast=False,
        sleep_before_exit=5)

    '''
