# Emulation

The emulation component uses Mininet for network emulation. The `setup.py` script currently creates a network that consists of multiple subdomains, each with a proxy and n clients. This script can be adapted to accommodate different scenarios as well. Additionally, the `demo.py` script provides a GUI to start different experiments and compare key metrics in real-time.

## Requirements

### Software Installation

1. **Mininet and Open vSwitch**
   - Installation instructions can be found on [Mininet's official site](http://mininet.org/download/).

2. **SMCRoute**
   - Install using the following command:
     ```commandline
     sudo apt install smcroute
     ```

3. **Selenium**
   - Install Selenium to run headless Firefox browsers:
     ```commandline
     sudo pip install selenium
     ```
   - Install Firefox and Geckodriver:
     - [Download Firefox](https://www.mozilla.org/en/firefox/new/)
     - [Download Geckodriver](https://github.com/mozilla/geckodriver/releases)

   - Ensure Firefox and Geckodriver are available as `/usr/local/bin/firefox` and `/usr/local/bin/geckodriver`. If not, create symbolic links to their actual locations.

4. **CustomTkinter and SciPy**
   - Install these packages to use the demo for network characteristics selection:
     ```commandline
     sudo pip install customtkinter scipy
     ```

5. **MPV**
   - Required to display two videos simultaneously without emulation:
     ```commandline
     sudo snap install mpv
     ```

## Running the Experiment

### Basic Setup
1. Run the setup script:
   ```commandline
   sudo python3 setup.py
   ```
   - Set the required parameters at the bottom of `setup.py`.

2. Use the demo application for a better experience:
   ```commandline
   sudo python3 demo.py
   ```
   - This opens a GUI to start different experiments and compare key metrics in real-time.
   - It is very important to read through this whole document before running the demo. (Especially the [Important Note](#important-note) section.)
   - Verify that the setup works using the setup script before running the demo.

### Important Note
Before running the demo, run the `setup.py` script once and execute the command from [Problem 5](#fix-firefox). This step is necessary to start the Firefox browser and has to be done each time you log in.

### Script Actions

The scripts perform the following tasks:
- Create a virtual network topology.
- Start network shaping.
- Launch the required components.
- Provide a Mininet terminal.
- Clean up resources upon completion.

## Network Topology

- A single network router interconnects multiple network domains.
- One domain contains the server; others contain proxies and clients.
- The controller supports multicast delivery to `239.0.0.1`.

### Network Links
- Proxies connect to the server using separate links, allowing GET requests without network shaping interference.

## Network Shaping

- The server connects to subdomains via a network controller with a traffic control (tc) enabled switch.
- Adjustable characteristics: Bandwidth, Latency, Packet loss.
- Use the `shape_bandwidth.sh` script to modify these settings.

## Components

### Server
- Supports unicast and multicast delivery (see [Server Component](../server)).
- Scripts: `server_http.sh` (unicast) and `server_multicast.sh` (multicast).

### Proxies
- Each subdomain has one proxy with IP `10.0.<proxy_id>.100`.
- Scripts: `proxy_http.sh` (HTTP caching), `proxy_multicast.sh` (multicast reception), `proxy_clear.sh` (clear cache).

### Clients
- The `client_run.sh` script starts a Firefox instance connecting to a proxy.

## Mininet Terminal

- Provides a master terminal to start Xterm on each node.
- Check if the `firefox` command works within the shell.

### Running Firefox Automation
```commandline
./client_run.sh 10.0.<proxy_id>.100:33333
```
- Ensure all resources load properly and check logs for smooth operation.

## Cleanup

- Terminate the experiment by running `exit` in the Mininet terminal.
- This releases all network resources and clears the proxies' caches.

## Fix Firefox Issues

### Problem 1
`internal error, please report, running firefox failed: cannot find tracking cgroup`
```commandline
sudo mount -t cgroup2 cgroup2 /sys/fs/cgroup
sudo mount -t securityfs securityfs /sys/kernel/security/
```

### Problem 2
`Error no decoder found for audio/mp4a-latm`
```commandline
sudo apt install ubuntu-restricted-extras
```

### Problem 3
`mkdir: cannot create directory '/run/user/0': Permission denied`
```commandline
sudo mkdir /run/user/0
```

### Problem 4
`Error: cannot open display:  :0`
- Reinstall Firefox manually following [Mozilla's guide](https://support.mozilla.org/en-US/kb/install-firefox-linux#w_system-firefox-installation-for-advanced-users).
- Alternatively, create a symbolic link so Firefox can be found at `/usr/local/bin`.

### Problem 5
`Running Firefox as root in a regular user's session is not supported.`
```commandline
export XAUTHORITY=$HOME/.Xauthority
```
- Run this command once per shell (e.g., in the Mininet terminal, open a client node shell using `xterm client_1_1`).

# Traces
[Dataset 4G](https://users.ugent.be/~jvdrhoof/dataset-4g/)