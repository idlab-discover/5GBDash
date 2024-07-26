import tkinter as tk
from tkinter import ttk
import customtkinter as ctk
from tkinter import messagebox
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import time
import signal
from threading import Thread, Event
import math
import random
import os
from scipy.interpolate import interp1d
import ast
import subprocess

import setup
import threading
import threading
import time

BOOT_TIME_MININET = 11
BOOT_TIME_MININET_LONG = 45

def convert_to_list(string_input, default_on_error=None):
    try:
        # Safely evaluate the string input
        nested_list = ast.literal_eval(string_input)
        return nested_list
    except (ValueError, SyntaxError) as e:
        print("Error:", e)
    return default_on_error

class Metric:
    def __init__(self, name, max_items=100, interval=1000, info={}):
        self.data = {}
        self._name = name
        self._info = info
        self._max_items = max_items
        self._interval = interval
        self._updated = False

    def __str__(self):
        latest_values = {}
        for key, value in self.data.items():
            if len(value) > 0:
                latest_values[key] = value[-1]
            else:
                latest_values[key] = None
        return f'{self._name}: {latest_values}'

    def __repr__(self):
        return self.__str__()

    def get_latest(self, key, default=None):
        if key in self.data:
            if len(self.data[key]) > 0:
                return self.data[key][-1]
        return default

    def name(self):
        return self._name

    def info(self, key=None, default=None):
        if key:
            return self._info.get(key, default)
        return self._info

    def get_max_items(self):
        return self._max_items

    def add(self, experiment, value):
        if experiment not in self.data:
            self.data[experiment] = []
        
        if isinstance(value, list):
            # Overwrite the current list
            self.data[experiment] = value
        elif len(self.data[experiment]) < self._max_items:
            # Append if we have less than _max_items
            self.data[experiment].append(value)
        else:
            # Remove the first item and append
            self.data[experiment].pop(0)
            self.data[experiment].append(value)

        self._updated = True

    def set_updated(self):
        self._updated = True

    def has_updated(self):
        return self._updated

    def reset_updated(self):
        self._updated = False

    def get_interval(self):
        return self._interval

    def rename_experiment(self, old_name, new_name):
        if old_name in self.data:
            self.data[new_name] = self.data.pop(old_name)
            self._updated = True

    def remove_experiment(self, experiment):
        if experiment in self.data:
            self.data.pop(experiment)
            self._updated = True

class Metrics:
    def __init__(self, max_items=100, interval=1000):
        self._metrics = {}
        self._max_items = max_items
        self._interval = interval
        self.current_bw = 0
        self.current_latency = 0
        self.current_loss = 0

        self.enable_max_y_group_values = False
        self.group_max_y_values = {}

        self.metrics_info = {
            'bandwidth_mc': {
                'title': 'Bandwidth (Multicast)',
                'unit': 'Mb/s',
                'description': 'The maximum rate of data transfer for multicast traffic',
                'legend_position': 'upper left',
                'group': 'bandwidth'
            },
            'bandwidth_uc': {
                'title': 'Bandwidth (Unicast)',
                'unit': 'Mb/s',
                'description': 'The maximum rate of data transfer for unicast traffic',
                'legend_position': 'upper left',
                'group': 'bandwidth'
            },
            'bandwidth_total': {
                'title': 'Bandwidth (Total)',
                'unit': 'Mb/s',
                'description': 'The maximum rate of data transfer for all traffic',
                'legend_position': 'upper left',
                'group': 'bandwidth'
            },
            'server_interface_0.transmitted;bytes_transmitted': {
                'title': 'Bytes transmitted (Multicast)',
                'unit': 'Bytes',
                'description': 'The amount of bytes transmitted by the server',
                'legend_position': 'upper left',
                'group': 'bytes'
            },
            'server_interface_1.transmitted;bytes_transmitted': {
                'title': 'Bytes transmitted (Unicast)',
                'unit': 'Bytes',
                'description': 'The amount of bytes transmitted by the server',
                'legend_position': 'upper left',
                'group': 'bytes'
            },
            'server_interface.transmitted;bytes_transmitted': {
                'title': 'Bytes transmitted (Total)',
                'unit': 'Bytes',
                'description': 'The amount of bytes transmitted by the server',
                'legend_position': 'upper left',
                'group': 'bytes'
            },
            'current_loss_mc': {
                'title': 'Packet loss (Multicast)',
                'unit': 'Percentage',
                'description': 'The loss for multicast traffic',
                'legend_position': 'center left',
                'group': 'loss'
            },
            'client_1_1;buffer_length': {
                'title': 'Buffer size',
                'unit': 'Seconds',
                'description': 'The size of the buffer used to store incoming packets',
                'legend_position': 'upper left',
                'group': 'buffer'
            },
            'client_1_1;live_latency': {
                'title': 'Live latency',
                'unit': 'Seconds',
                'description': 'The time it takes for a packet to travel from the source to the destination',
                'legend_position': 'lower right',
                'group': 'latency'
            }
        }

        for name, info in self.metrics_info.items():
            self._metrics[name] = Metric(name, max_items=max_items, interval=interval, info=info)

    def __getitem__(self, name):
        return self.get(name)

    def add(self, experiment, metric_name, value):
        metric = self.get(metric_name)
        if metric is None:
            return
        metric.add(experiment, value)

    def get(self, name):
        if name not in self._metrics:
            self._metrics[name] = Metric(name, max_items=self._max_items, interval=self._interval)
        return self._metrics[name]

    def get_metric_names(self):
        return self._metrics.keys()
        
    def get_interval(self):
        return self._interval

    def get_max_items(self):
        return self._max_items

    def set_all_to_updated(self):
        for metric in self._metrics.values():
            metric.set_updated()

    def rename_experiment(self, old_name, new_name):
        for metric in self._metrics.values():
            metric.rename_experiment(old_name, new_name)

    def remove_experiment(self, experiment):
        for metric in self._metrics.values():
            metric.remove_experiment(experiment)

    def get_experiment_names(self):
        experiment_names = set()
        for metric in self._metrics.values():
            experiment_names.update(metric.data.keys())
        return list(experiment_names)

    def reset_group_max_y_values(self):
        self.group_max_y_values = {}

    def get_group_max_y_value(self, group, new_value_if_greater=None):
        # If this feature is disabled, return the input value
        if not self.enable_max_y_group_values:
            return new_value_if_greater

        if group in self.group_max_y_values:
            # If the new value is greater, then update the value
            if new_value_if_greater is not None:
                self.group_max_y_values[group] = max(self.group_max_y_values[group], new_value_if_greater)
            return self.group_max_y_values[group]

        # Add the group to the dict
        self.group_max_y_values[group] = new_value_if_greater
        return new_value_if_greater

class Graph:
    LEGEND_POSITIONS = [
        'upper left', 'upper center', 'upper right', 'center right', 'lower right',
        'lower center', 'lower left', 'center left', 'center'
    ]

    def __init__(self, master, metric, metrics = None, **kwargs):
        self.master = master
        self.metric = metric
        self.metrics = metrics
        self.unit = metric.info('unit', '')
        self.title = metric.info('title', metric.name())
        self.legend_position = metric.info('legend_position', 'best')
        self.legend_visible = True
        
        # Extract width and height from kwargs, defaulting to None if not provided
        width = kwargs.pop('width', None)
        height = kwargs.pop('height', None)

        self.fig, self.ax = plt.subplots()
        plt.subplots_adjust(right=0.9, top=0.9, bottom=0.2)
        self.fig.patch.set_alpha(0)
        self.ax.set_title(self.title)

        # Set the minimum value for the y-axis to 0
        self.ax.set_ylim(bottom=0)
        # Set the x axis to the time in seconds
        self.ax.set_xlabel('Time (s)')
        # Set the min and max values for the x-axis
        self.ax.set_xlim(left=0)
        self.ax.set_ylabel(self.unit)

        self.canvas = FigureCanvasTkAgg(self.fig, master=self.master)
        self.canvas.get_tk_widget().grid(kwargs)
        self.canvas.get_tk_widget().config(bg=master['bg'])

        # Bind the mouse click events to the canvas
        self.canvas.get_tk_widget().bind("<Button-1>", self.on_canvas_left_click)
        self.canvas.get_tk_widget().bind("<Button-3>", self.on_canvas_right_click)

        # Configure width and height if provided
        if width is not None and height is not None:
            self.canvas.get_tk_widget().config(width=width, height=height)
        
        self.canvas.draw()

        self.update_content()

        # Add a value as well below the graph
        #self.value_label = ctk.CTkLabel(self.master, text='', width=10, height=2)
        #self.value_label.grid(row=1, column=0, padx=3, pady=3)

    def on_canvas_left_click(self, event):
        self.cycle_legend_position()

    def on_canvas_right_click(self, event):
        self.toggle_legend_visibility()

    def destroy(self):
        self.canvas.get_tk_widget().destroy()

    def update_window(self, width, height, redraw=True):
        self.canvas.get_tk_widget().config(width=width, height=height)
        if redraw:
            self.canvas.draw()

    def cycle_legend_position(self):
        if not self.legend_visible:
            self.toggle_legend_visibility()
            return
        if not self.metric.data or len(self.metric.data) < 1:
            # The legend will be hidden, so we don't need to cycle the position
            return
        try:
            current_index = self.LEGEND_POSITIONS.index(self.legend_position)
        except Exception as e:
            current_index = -1 # Fall back to the first position, this could happen when the position is 'best'
        next_index = (current_index + 1) % len(self.LEGEND_POSITIONS)
        self.legend_position = self.LEGEND_POSITIONS[next_index]
        self.update_legend()

    def toggle_legend_visibility(self):
        self.legend_visible = not self.legend_visible
        self.update_legend()

    def update_legend(self, draw=True):
        if self.legend_visible and self.metric.data and len(self.metric.data) >= 1:
            self.ax.legend(loc=self.legend_position)
        else:
            legend = self.ax.get_legend()
            if legend:
                legend.remove()
        if draw:
            self.canvas.draw()

    def update_content(self, force_update=False):
        if self.metric.has_updated() or force_update:
            self.ax.clear()
            allow_mbps_rescale = False

            max_items = self.metric.get_max_items()
            max_y = 0 if allow_mbps_rescale else 1
            min_y = None
            max_x = 1
            for name, y_known in self.metric.data.items():
                if len(y_known) == 0:
                    continue
                max_y = max(max_y, max(y_known))
                min_y = min(min_y, min(y_known)) if min_y is not None else min(y_known)
                max_x = max(max_x, len(y_known))

            if max_y == 0 and min_y == 0:
                max_y = 1

            group = self.metric.info('group')
            if group is not None and self.metrics is not None:
                max_y = self.metrics.get_group_max_y_value(group, max_y)

            # Set the title
            self.ax.set_title(self.title)
            # Set the x axis to the time in seconds
            self.ax.set_xlabel('Time (s)')
            divider = 1
            if self.unit.lower() == 'bytes':
                # Get the order of magnitude of the max value
                if max_y > 1_000_000_000:
                    self.ax.set_ylabel('GB')
                    divider = 1_000_000_000
                elif max_y > 1_000_000:
                    self.ax.set_ylabel('MB')
                    divider = 1_000_000
                elif max_y > 1_000:
                    self.ax.set_ylabel('KB')
                    divider = 1_000
                else:
                    self.ax.set_ylabel('Bytes')
            elif allow_mbps_rescale and self.unit.lower() == 'mb/s':
                print(self.unit.lower(), max_y)
                # Get the order of magnitude of the max value
                if max_y > 1_000:
                    self.ax.set_ylabel('Gb/s')
                    divider = 1_000
                elif max_y < 1:
                    self.ax.set_ylabel('Kb/s')
                    divider = 1 / 1_000
                else:
                    self.ax.set_ylabel('Mb/s')
            else:
                self.ax.set_ylabel(self.unit)
                
            
            if min_y is None:
                min_y = 0

            top_modifier = 4 / 3 if min_y != max_y else 2
            if min_y > 0:
                min_y = 0
            self.ax.set_ylim(bottom=min_y, top=max_y * top_modifier / divider)
            self.ax.set_xlim(left=0, right=max_x)

            for name, y_known in self.metric.data.items():
                if len(y_known) == 0:
                    continue
                # Create a copy of y_known and divide by the divider
                y_divided = [y / divider for y in y_known]
                p = self.ax.plot(y_divided, label=name)            

            self.update_legend(draw=False)
            self.canvas.draw()
            self.metric.reset_updated()

class Experiment:
    def __init__(self, master, metrics, name):
        self._master = master
        self._metrics = metrics
        self._interval = metrics.get_interval()
        self._name = name
        self._running = False
        self._running_time = 0
        self._thread = None
        self._boot_thread = None
        self._start_await_flag = Event()
        self._stop_flag = Event()
        self._mc_boot_time = 0
        self._clients_time_offset = 0
        self._max_experiment_time_after_boot = metrics.get_max_items()
        self._mc_enabled = True
        self._boot_time_mininet = BOOT_TIME_MININET

    def __str__(self):
        return f'Experiment: {self._name}'
    
    def __repr__(self):
        return self.__str__()
    
    def name(self):
        return self._name

    def start(self, **kwargs):
        print('Starting experiment')
        print(kwargs)

        self._start_experiment(**kwargs)

        self._running = True
        self._master.after(self._interval, self.update_content)

    def stop(self):
        self._running = False
        print('Stopping experiment')

        if not self.booted():
            # Remove the metrics for this experiment
            self._metrics.remove_experiment(self._name)

        self._stop_experiment()

    def _start_experiment(self, **kwargs):
        self._mc_boot_time = kwargs.pop('mc_boot_time', 0)
        self._clients_time_offset = kwargs.pop('clients_time_offset', 0)
        self._mc_enabled = kwargs.get('amount_to_multicast', 0) > 0

        n_clients = kwargs.get('n_clients', 0)
        n_subnets = kwargs.get('n_subnets', 0)

        if n_clients * n_subnets >= 5:
            self._boot_time_mininet = BOOT_TIME_MININET_LONG
            print('Using long boot time')


        def start_experiment_thread():
            self._start_await_flag.clear()
            self._stop_flag.clear()
            # Add the stop flag to the kwargs
            kwargs['start_await_flag'] = self._start_await_flag
            kwargs['stop_flag'] = self._stop_flag
            kwargs['enable_CLI'] = False
            kwargs['wait_for_multicast_to_finish'] = False
            kwargs['wait_for_clients_to_finish'] = False
            setup.run_experiment(**kwargs)

        self._thread = threading.Thread(target=start_experiment_thread)
        self._thread.start()

        def _boot_experiment():
            # Wait BOOT_TIME_MININET seconds for the experiment to boot in a loop
            for _ in range(self._boot_time_mininet):
                if self._start_await_flag.is_set() or self._stop_flag.is_set():
                    return
                time.sleep(1)
            # All the nodes have booted, let's start the experiment
            print('\nNotes have booted.\n')
            self._start_await_flag.set()

        # Run the booting process in a separate thread
        self._boot_thread = threading.Thread(target=_boot_experiment)
        self._boot_thread.start()

    def _stop_experiment(self):
        self._start_await_flag.set()
        self._stop_flag.set()
        if self._boot_thread and self._boot_thread.is_alive():
            self._boot_thread.join()
        if self._thread and self._thread.is_alive():
            self._thread.join()

    def update_content(self):
        if not self._running:
            return

        self._running_time += self._interval / 1000

        self.update_metrics()

        self._master.after(self._interval, self.update_content)

    def update_metrics(self):
        if (not self.booted()) or self.completed():
            return

        # Read all the *.metrics files and add the values to the metrics
        # First we have to find the metrics files
        # These are placed within multiple directories, so we have to search for them
        metrics_files = []
        for root, dirs, files in os.walk('../'):
            # Skip the evaluation directory
            if 'evaluation' in root:
                continue

            for file in files:
                if file.endswith('.metrics'):
                    metrics_files.append(os.path.join(root, file))
        
        # Read the metrics files and add the values to the metrics
        # We create a dict first in case we have multiple values for the same metric
        updates = {}
        for file in metrics_files:
            with open(file, 'r') as f:
                for line in f:
                    parts = line.split(';')
                    if len(parts) == 2:
                        filename = file.split("/")[-1]
                        # Remove the .metrics extension
                        filename = '.'.join(filename.split(".")[:-1])
                        key = f'{filename};{parts[0]}'
                        try:
                            updates[key] = eval(parts[1])
                        except (SyntaxError, ZeroDivisionError, TypeError) as e:
                            print(f'Error: {e}')
                            updates[key] = 0

        bytes_transmitted_mc_key = "server_interface_0.transmitted;bytes_transmitted"
        if not self._mc_enabled:
            updates[bytes_transmitted_mc_key] = 0

        current_bytes_mc = 0
        if 'bandwidth_mc' not in updates:
             # Interface 0 is the multicast interface
            bytes_transmitted_key = bytes_transmitted_mc_key
            previous_bytes_mc = self._metrics[bytes_transmitted_key].get_latest(self._name, 0)
            current_bytes_mc = updates.get(bytes_transmitted_key, previous_bytes_mc)
            difference = current_bytes_mc - previous_bytes_mc
            transmission_time = self._interval / 1000 # seconds
            if transmission_time > 0:
                # Calculate the bandwidth in Mbps
                updates['bandwidth_mc'] = difference * 8 / transmission_time / 1_000_000
            else:
                # The window is too small to calculate the bandwidth, so we just take the latest value
                updates['bandwidth_mc'] = self._metrics['bandwidth_mc'].get_latest(self._name, 0)

        bytes_transmitted_uc_key = "server_interface_1.transmitted;bytes_transmitted"
        current_bytes_uc = 0
        if 'bandwidth_uc' not in updates:
             # Interface 1 is the unicast interface
            bytes_transmitted_key = bytes_transmitted_uc_key
            previous_bytes_uc = self._metrics[bytes_transmitted_key].get_latest(self._name, 0)
            current_bytes_uc = updates.get(bytes_transmitted_key, previous_bytes_uc)
            difference = current_bytes_uc - previous_bytes_uc
            transmission_time = self._interval / 1000 # seconds
            if transmission_time > 0:
                # Calculate the bandwidth in Mbps
                updates['bandwidth_uc'] = difference * 8 / transmission_time / 1_000_000
            else:
                # The window is too small to calculate the bandwidth, so we just take the latest value
                updates['bandwidth_uc'] = self._metrics['bandwidth_uc'].get_latest(self._name, 0)

        if 'bandwidth_total' not in updates:
            updates['bandwidth_total'] = updates.get('bandwidth_mc', 0) + updates.get('bandwidth_uc', 0)

        if 'server_interface.transmitted;bytes_transmitted' not in updates:
            updates['server_interface.transmitted;bytes_transmitted'] = current_bytes_mc + current_bytes_uc

        if 'current_loss_mc' not in updates:
            updates['current_loss_mc'] = self._metrics.current_loss

        # print(updates)

        for key, value in updates.items():
            if key.endswith('live_latency'):
                if value > 0:
                    value += self._clients_time_offset
            self._metrics.add(self._name, key, value)

        for key in self._metrics.get_metric_names():
            if key not in updates:
                # Get the previous value and add it to the metrics
                self._metrics.add(self._name, key, self._metrics[key].get_latest(self._name, 0))

    def rename(self, new_name):
        self._metrics.rename_experiment(self._name, new_name)
        self._name = new_name

    def time(self):
        return self._running_time

    def completed(self):
        mc_boot_time_offset = self._mc_boot_time / 2 if self._mc_boot_time > 0 else 0
        return not self._running or self._running_time > self._max_experiment_time_after_boot + self._boot_time_mininet + mc_boot_time_offset

    def booted(self):
        mc_boot_time_offset = self._mc_boot_time / 2 if self._mc_boot_time > 0 else 0
        return self._running_time > self._boot_time_mininet + mc_boot_time_offset

    def metrics_dict(self):
        # This creates a dict with the metrics for this experiment
        metrics_dict = {}
        for key in self._metrics.get_metric_names():
            l = self._metrics[key].data.get(self._name, [])
            # Only add the metric if it has values
            if len(l) > 0:
                metrics_dict[key] = l

        return metrics_dict

class IntSpinbox(ctk.CTkFrame):
    def __init__(self, *args,
                 width: int = 100,
                 height: int = 32,
                 step_size: int = 1,
                 text=None,
                 min_value=None,
                 max_value=None,
                 command = None,
                 action = None,
                 **kwargs):
        super().__init__(*args, width=width, height=height, **kwargs)

        self.step_size = step_size
        self.command = command
        self.action = action

        self.min_value = min_value
        self.max_value = max_value

        self.configure(fg_color=("gray78", "gray28"))  # set frame color

        offset = 0

        if text is not None:
            offset += 1
            self.text_label = ctk.CTkLabel(self, text=text, width=10, height=height-6)
            self.text_label.grid(row=0, column=0, padx=3, pady=3)
            self.grid_columnconfigure(0, weight=1)  # label expand

        self.grid_columnconfigure((0 + offset, 1 + offset, 2 + offset), weight=0)  # buttons don't expand
        #self.grid_columnconfigure(1 + offset, weight=1)  # entry expands

        self.subtract_button = ctk.CTkButton(self, text="-", width=height-6, height=height-6,
                                                       command=self.subtract_button_callback)
        self.subtract_button.grid(row=0, column=0 + offset, padx=(3, 0), pady=3)

        self.entry = ctk.CTkEntry(self, width=width-(1.75*height), height=height-6, border_width=0)
        self.entry.grid(row=0, column=1 + offset, columnspan=1, padx=3, pady=3, sticky="ew")

        self.add_button = ctk.CTkButton(self, text="+", width=height-6, height=height-6,
                                                  command=self.add_button_callback)
        self.add_button.grid(row=0, column=2 + offset, padx=(0, 3), pady=3)

        # default value
        self.entry.insert(0, "0")

    def _convert_to_int(self, value):
        # Convert to string and remove all characters that are not numbers or .
        # Only keep numerical values (including .)
        value = ''.join(filter(lambda x: x.isdigit() or x == '.', str(value)))
        # Conver to float if it contains a .
        if '.' in value:
            if len(value) == 1:
                return 0
            else:
                value = float(value)
                # Floor the value and convert to int
                return int(math.floor(value))
        elif len(value) == 0:
            return 0

        return int(value)

    def add_button_callback(self):
        if self.command is not None:
            self.command()

        original_value = self.entry.get()
        try:
            original_value = int(original_value)
        except ValueError:
            original_value = self._convert_to_int(original_value)

        value = original_value + self.step_size
        if self.max_value is not None and value > self.max_value:
            value = self.max_value
        self.entry.delete(0, "end")
        self.entry.insert(0, value)
        if self.action is not None:
            self.action()


    def subtract_button_callback(self):
        if self.command is not None:
            self.command()

        original_value = self.entry.get()
        try:
            original_value = int(original_value)
        except ValueError:
            original_value = self._convert_to_int(original_value)


        value = original_value - self.step_size
        if self.min_value is not None and value < self.min_value:
            value = self.min_value
        self.entry.delete(0, "end")
        self.entry.insert(0, value)
        if self.action is not None:
            self.action()

    def get(self):            
        value = self.entry.get()
        try:
            value = int(value)
        except ValueError:
            value = self._convert_to_int(value)

        if self.min_value is not None and value < self.min_value:
            value = self.min_value
        if self.max_value is not None and value > self.max_value:
            value = self.max_value

        if str(value) != self.entry.get():
            # The value was changed, update the entry
            self.entry.delete(0, "end")
            self.entry.insert(0, value)

        return value

    def set(self, value: int):
        try:
            value = int(value)
        except ValueError:
            value = self._convert_to_int(value)

        if self.min_value is not None and value < self.min_value:
             value = self.min_value
        if self.max_value is not None and value > self.max_value:
            value = self.max_value
            
        self.entry.delete(0, "end")
        self.entry.insert(0, value)

class FloatSpinbox(ctk.CTkFrame):
    def __init__(self, *args,
                 width: int = 100,
                 height: int = 32,
                 step_size: float = 0.1,
                 text=None,
                 min_value=None,
                 max_value=None,
                 **kwargs):
        super().__init__(*args, width=width, height=height, **kwargs)

        self.step_size = step_size

        self.min_value = min_value
        self.max_value = max_value

        self.configure(fg_color=("gray78", "gray28"))  # set frame color

        offset = 0

        if text is not None:
            offset += 1
            self.text_label = ctk.CTkLabel(self, text=text, width=10, height=height-6)
            self.text_label.grid(row=0, column=0, padx=3, pady=3)
            self.grid_columnconfigure(0, weight=1)  # label expand

        self.grid_columnconfigure((0 + offset, 1 + offset, 2 + offset), weight=0)  # buttons don't expand
        #self.grid_columnconfigure(1 + offset, weight=1)  # entry expands

        self.subtract_button = ctk.CTkButton(self, text="-", width=height-6, height=height-6,
                                                       command=self.subtract_button_callback)
        self.subtract_button.grid(row=0, column=0 + offset, padx=(3, 0), pady=3)

        self.entry = ctk.CTkEntry(self, width=width-(1.75*height), height=height-6, border_width=0)
        self.entry.grid(row=0, column=1 + offset, columnspan=1, padx=3, pady=3, sticky="ew")

        self.add_button = ctk.CTkButton(self, text="+", width=height-6, height=height-6,
                                                  command=self.add_button_callback)
        self.add_button.grid(row=0, column=2 + offset, padx=(0, 3), pady=3)

        # default value
        self.entry.insert(0, "0")

    
    def _convert_to_float(self, value):
        # Convert to string and remove all characters that are not numbers or .
        # Only keep numerical values (including .)
        value = ''.join(filter(lambda x: x.isdigit() or x == '.', str(value)))

        if len(value) == 0 or value == '.':
            return 0.0

        return float(value)

    def add_button_callback(self):
        original_value = self.entry.get()
        try:
            original_value = float(original_value)
        except ValueError:
            original_value = self._convert_to_float(original_value)

        value = original_value + self.step_size
        if self.max_value is not None and value > self.max_value:
            value = self.max_value
        self.entry.delete(0, "end")
        self.entry.insert(0, value)

    
    def subtract_button_callback(self):
        original_value = self.entry.get()
        try:
            original_value = float(original_value)
        except ValueError:
            original_value = self._convert_to_float(original_value)

        value = original_value - self.step_size
        if self.min_value is not None and value < self.min_value:
            value = self.min_value
        self.entry.delete(0, "end")
        self.entry.insert(0, value)

    def get(self):
        value = self.entry.get()
        try:
            value = float(value)
        except ValueError:
            value = self._convert_to_float(value)
            
        if self.min_value is not None and value < self.min_value:
             value = self.min_value
        if self.max_value is not None and value > self.max_value:
            value = self.max_value

        if str(value) != self.entry.get():
            # The value was changed, update the entry
            self.entry.delete(0, "end")
            self.entry.insert(0, value)
        
        return value

    def set(self, value: float):
        try:
            value = float(value)
        except ValueError:
            value = self._convert_to_float(value)

        if self.min_value is not None and value < self.min_value:
             value = self.min_value
        if self.max_value is not None and value > self.max_value:
            value = self.max_value

        self.entry.delete(0, "end")
        self.entry.insert(0, value)

class TextInputBox(ctk.CTkFrame):
    def __init__(self, *args, width: int = 100, height: int = 32, text=None, default_text=None, **kwargs):
        super().__init__(*args, width=width, height=height, **kwargs)

        self.configure(fg_color=("gray78", "gray28"))  # set frame color

        self.grid_columnconfigure((0,1), weight=1)  # entry expands

        if text is not None:
            self.text_label = ctk.CTkLabel(self, text=text, width=10, height=height-6)
            self.text_label.grid(row=0, column=0, padx=3, pady=3)
            self.grid_columnconfigure(0, weight=1)  # label expand

        self.entry = ctk.CTkEntry(self, width=width, height=height-6, border_width=0)
        self.entry.grid(row=0, column=1, columnspan=1, padx=3, pady=3, sticky="ew")

        # default value
        if default_text is not None:
            self.entry.insert(0, default_text)

    def get(self):
        return self.entry.get()

    def set(self, value: str):
        self.entry.delete(0, "end")
        self.entry.insert(0, value)

class CheckBox(ctk.CTkFrame):
    def __init__(self, *args, width: int = 100, height: int = 32, text=None, variable=None, action = None, **kwargs):
        super().__init__(*args, width=width, height=height, **kwargs)

        self.configure(fg_color=("gray78", "gray28"))  # set frame color

        self.action = action

        offset = 0

        # Add text
        if text is not None:
            offset += 1
            self.text_label = ctk.CTkLabel(self, text=text, width=10, height=height-6)
            self.text_label.grid(row=0, column=0, padx=3, pady=3)
            self.grid_columnconfigure(0, weight=1)  # Expand


        self.grid_columnconfigure(offset, weight=0)  # Don't expand


        self.checkbox = ctk.CTkCheckBox(self, width=10, text="", height=height-6, variable=variable, command=self.action)
        self.checkbox.grid(row=0, column= offset, padx=3, pady=3)

    def get(self):
        result = self.checkbox.get()
        if result == '1' or result == 1 or result == True:
            return True
        return False

    def set(self, value: bool):
        #print('Setting checkbox',self.text_label.cget('text'),'to', value)
        if value:
            self.checkbox.select()
        else:
            self.checkbox.deselect()

        if self.action is not None:
            self.action()

class Frame(ctk.CTkScrollableFrame):
    def __init__(self, master, metrics, **kwargs):
        super().__init__(master, **kwargs)
        # self.master = self._parent_canvas
        self._metrics = metrics
        self._interval = metrics.get_interval()

        # Bind the mouse wheel to the scrollable frame
        self.bind('<Enter>', self._bound_to_mousewheel)
        self.bind('<Leave>', self._unbound_to_mousewheel)

    def start(self):
        pass

    def update_content(self):
        pass

    def _bound_to_mousewheel(self, event):
        self.bind_all("<Button-4>", self._on_mousewheel) # Up
        self.bind_all("<Button-5>", self._on_mousewheel) # Down

    def _unbound_to_mousewheel(self, event):
        self.unbind_all("<Button-4>")
        self.unbind_all("<Button-5>")

    def _on_mousewheel(self, event):
        event.delta = -1 if event.num == 5 else 1 if event.num == 4 else 0
        self._mouse_wheel_all(event) # Internal function

class ExperimentFrame(Frame):
    def __init__(self, master, metrics, **kwargs):
        super().__init__(master, metrics, **kwargs)
        self.app = master
        self._current_experiment = None
        self.past_experiments = []
        self._current_has_booted = False
        self.starting_experiment = False
        self._interval = 1000 # Force the update interval to 1 second, this is needed for the progres bar to work correctly
        self._fast_interval = 10  # Temporary faster interval
        self._normal_interval = self._interval  # The normal interval
        self._stabilize_counter = 5  # Number of stable updates before reverting to normal interval
        self._stabilize_count = 0
        self._last_size = (0, 0)
        self.default_experiments = {
            '======HYBRID======': {
                'title': '======== HYBRID =========',
            },
            '01_only_unicast_4_sec': {
                'title': '1-1 Only unicast (4 sec)',
                'bandwidth_mc': 25,
                'latency': 0,
                'loss': 0,
                'n_clients': 1,
                'n_proxies': 1,
                'show_first_client': True,
                'show_second_client': False,
                'fec': False,
                'low_latency': False,
                'client_start_offset': 4,
                'live_latency_goal': 8.1,
                'enable_tli': False,
                'videos': "[['avatar_midhigh_1']]",
                'extensions': "['.mp4']",
                'representation_ids': "['4']",
                'amount_to_multicast': 0,
                'seg_duration': 4,
                'chunk_count': 1,
                'mc_start_spread': 1,
            },
            '02_hybrid_4_sec': {
                'title': '1-1 Hybrid (4 sec)',
                'bandwidth_mc': 25,
                'latency': 0,
                'loss': 0,
                'n_clients': 1,
                'n_proxies': 1,
                'show_first_client': True,
                'show_second_client': False,
                'fec': False,
                'low_latency': False,
                'client_start_offset': 4,
                'live_latency_goal': 8.1,
                'enable_tli': False,
                'videos': "[['avatar_midhigh_1']]",
                'extensions': "['.mp4']",
                'representation_ids': "['4']",
                'amount_to_multicast': 1,
                'seg_duration': 4,
                'chunk_count': 1,
                'mc_start_spread': 1,
            },
            '03_hybrid_4_sec_two_proxies': {
                'title': '2-1 Hybrid (4 sec)',
                'bandwidth_mc': 25,
                'latency': 0,
                'loss': 0,
                'n_clients': 1,
                'n_proxies': 2,
                'show_first_client': True,
                'show_second_client': False,
                'fec': False,
                'low_latency': False,
                'client_start_offset': 4,
                'live_latency_goal': 8.1,
                'enable_tli': False,
                'videos': "[['avatar_midhigh_1']]",
                'extensions': "['.mp4']",
                'representation_ids': "['4']",
                'amount_to_multicast': 1,
                'seg_duration': 4,
                'chunk_count': 1,
                'mc_start_spread': 1,
            },
            '04_hybrid_4_sec_lossy': {
                'title': '1-1 Hybrid (4 sec) with 1% loss',
                'bandwidth_mc': 25,
                'latency': 0,
                'loss': 1,
                'n_clients': 1,
                'n_proxies': 1,
                'show_first_client': True,
                'show_second_client': False,
                'fec': False,
                'low_latency': False,
                'client_start_offset': 4,
                'live_latency_goal': 8.1,
                'enable_tli': False,
                'videos': "[['avatar_midhigh_1']]",
                'extensions': "['.mp4']",
                'representation_ids': "['4']",
                'amount_to_multicast': 1,
                'seg_duration': 4,
                'chunk_count': 1,
                'mc_start_spread': 1,
            },
            '05_hybrid_4_sec_lossy_two_proxies': {
                'title': '2-1 Hybrid (4 sec) with 1% loss',
                'bandwidth_mc': 25,
                'latency': 0,
                'loss': 1,
                'n_clients': 1,
                'n_proxies': 2,
                'show_first_client': True,
                'show_second_client': False,
                'fec': False,
                'low_latency': False,
                'client_start_offset': 4,
                'live_latency_goal': 8.1,
                'enable_tli': False,
                'videos': "[['avatar_midhigh_1']]",
                'extensions': "['.mp4']",
                'representation_ids': "['4']",
                'amount_to_multicast': 1,
                'seg_duration': 4,
                'chunk_count': 1,
                'mc_start_spread': 1,
            },
            '06_hybrid_4_sec_lossy_fec': {
                'title': '1-1 Hybrid (4 sec) with 1% loss and FEC',
                'bandwidth_mc': 25,
                'latency': 0,
                'loss': 1,
                'n_clients': 1,
                'n_proxies': 1,
                'show_first_client': True,
                'show_second_client': False,
                'fec': True,
                'low_latency': False,
                'client_start_offset': 4,
                'live_latency_goal': 8.1,
                'enable_tli': False,
                'videos': "[['avatar_midhigh_1']]",
                'extensions': "['.mp4']",
                'representation_ids': "['4']",
                'amount_to_multicast': 1,
                'seg_duration': 4,
                'chunk_count': 1,
                'mc_start_spread': 1,
            },
            '07_hybrid_4_sec_lossy_fec_two_proxies': {
                'title': '2-1 Hybrid (4 sec) with 1% loss and FEC',
                'bandwidth_mc': 25,
                'latency': 0,
                'loss': 1,
                'n_clients': 1,
                'n_proxies': 2,
                'show_first_client': True,
                'show_second_client': False,
                'fec': True,
                'low_latency': False,
                'client_start_offset': 4,
                'live_latency_goal': 8.1,
                'enable_tli': False,
                'videos': "[['avatar_midhigh_1']]",
                'extensions': "['.mp4']",
                'representation_ids': "['4']",
                'amount_to_multicast': 1,
                'seg_duration': 4,
                'chunk_count': 1,
                'mc_start_spread': 1,
            },
            '======TLI======': {
                'title': '======== TLI =========',
            },
            '08_hybrid_mid_no_tli': {
                'title': '2-1 Hybrid (Quality 3)',
                'bandwidth_mc': 25,
                'latency': 0,
                'loss': 0,
                'n_clients': 1,
                'n_proxies': 2,
                'show_first_client': False,
                'show_second_client': False,
                'fec': False,
                'low_latency': False,
                'client_start_offset': 3,
                'live_latency_goal': 8.1,
                'enable_tli': False,
                'videos': "[['avatar_mid_1'],['avatar_mid_1']]",
                'extensions': "['.mp4']",
                'representation_ids': "['3']",
                'amount_to_multicast': 1,
                'seg_duration': 4,
                'chunk_count': 1,
                'mc_start_spread': 1,
            },
            '09_hybrid_mid_to_midhigh_tli': {
                'title': '2-1 Hybrid (Quality 3 to 4 TLI)',
                'bandwidth_mc': 25,
                'latency': 0,
                'loss': 0,
                'n_clients': 1,
                'n_proxies': 2,
                'show_first_client': False,
                'show_second_client': False,
                'fec': False,
                'low_latency': False,
                'client_start_offset': 3,
                'live_latency_goal': 8.1,
                'enable_tli': True,
                'videos': "[['avatar_mid_1'],['avatar_mid_1']]",
                'extensions': "['.mp4']",
                'representation_ids': "['3', '3']",
                'amount_to_multicast': 1,
                'seg_duration': 4,
                'chunk_count': 1,
                'mc_start_spread': 1,
            },
            '=====LARGE_SCALE=====': {
                'title': '===== LARGE SCALE =====',
            },
            '10_only_unicast_25': {
                'title': '5-5 Only unicast (25 vids)',
                'bandwidth_mc': 25,
                'latency': 0,
                'loss': 0,
                'n_clients': 5,
                'n_proxies': 5,
                'show_first_client': False,
                'show_second_client': False,
                'fec': False,
                'low_latency': False,
                'client_start_offset': 3,
                'live_latency_goal': 8.1,
                'enable_tli': False,
                'videos': "[['avatar_midhigh_1', 'avatar_midhigh_1', 'avatar_midhigh_2', 'avatar_midhigh_4', 'avatar_midhigh_5'],['avatar_midhigh_1', 'avatar_midhigh_1', 'avatar_midhigh_2', 'avatar_midhigh_4', 'avatar_midhigh_6'],['avatar_midhigh_1', 'avatar_midhigh_1', 'avatar_midhigh_2', 'avatar_midhigh_7', 'avatar_midhigh_8'],['avatar_midhigh_1', 'avatar_midhigh_1', 'avatar_midhigh_3', 'avatar_midhigh_9', 'avatar_midhigh_10'],['avatar_midhigh_1', 'avatar_midhigh_2', 'avatar_midhigh_3', 'avatar_midhigh_11', 'avatar_midhigh_12']]",
                'extensions': "['.mp4']",
                'representation_ids': "['4']",
                'amount_to_multicast': 0,
                'seg_duration': 4,
                'chunk_count': 1,
                'mc_start_spread': 1,
            },
            '11_hybrid_top_3_of_25': {
                'title': '5-5 Hybrid (Top 3 of 25 vids)',
                'bandwidth_mc': 25,
                'latency': 0,
                'loss': 0,
                'n_clients': 5,
                'n_proxies': 5,
                'show_first_client': False,
                'show_second_client': False,
                'fec': False,
                'low_latency': False,
                'client_start_offset': 3,
                'live_latency_goal': 8.1,
                'enable_tli': False,
                'videos': "[['avatar_midhigh_1', 'avatar_midhigh_1', 'avatar_midhigh_2', 'avatar_midhigh_4', 'avatar_midhigh_5'],['avatar_midhigh_1', 'avatar_midhigh_1', 'avatar_midhigh_2', 'avatar_midhigh_4', 'avatar_midhigh_6'],['avatar_midhigh_1', 'avatar_midhigh_1', 'avatar_midhigh_2', 'avatar_midhigh_7', 'avatar_midhigh_8'],['avatar_midhigh_1', 'avatar_midhigh_1', 'avatar_midhigh_3', 'avatar_midhigh_9', 'avatar_midhigh_10'],['avatar_midhigh_1', 'avatar_midhigh_2', 'avatar_midhigh_3', 'avatar_midhigh_11', 'avatar_midhigh_12']]",
                'extensions': "['.mp4']",
                'representation_ids': "['4']",
                'amount_to_multicast': 3,
                'seg_duration': 4,
                'chunk_count': 1,
                'mc_start_spread': 1,
            },
            '==== LOW-LATENCY ====': {
                'title': '==== LOW-LATENCY ====',
            },
            '12_hybrid_4_sec_ll': {
                'title': '1-1 Hybrid (4 sec) low-latency',
                'bandwidth_mc': 100,
                'latency': 0,
                'loss': 0,
                'n_clients': 1,
                'n_proxies': 1,
                'show_first_client': True,
                'show_second_client': False,
                'fec': False,
                'low_latency': False,
                'client_start_offset': 1,
                'live_latency_goal': 5.5,
                'enable_tli': False,
                'videos': "[['avatarcmaf1_1']]",
                'extensions': "['.m4s']",
                'representation_ids': "['5']",
                'amount_to_multicast': 1,
                'seg_duration': 4,
                'chunk_count': 1,
                'mc_start_spread': 1,
            },
            '13_hybrid_1_sec_ll': {
                'title': '1-1 Hybrid (1 sec) low-latency',
                'bandwidth_mc': 100,
                'latency': 0,
                'loss': 0,
                'n_clients': 1,
                'n_proxies': 1,
                'show_first_client': True,
                'show_second_client': False,
                'fec': False,
                'low_latency': False,
                'client_start_offset': 1,
                'live_latency_goal': 3.0,
                'enable_tli': False,
                'videos': "[['avatarcmaf1_1']]",
                'extensions': "['.m4s']",
                'representation_ids': "['5']",
                'amount_to_multicast': 1,
                'seg_duration': 1,
                'chunk_count': 1,
                'mc_start_spread': 1,
            },
            '14_hybrid_0_5_sec_ll': {
                'title': '1-1 Hybrid (4 sec, 8 chunks) CMAF',
                'bandwidth_mc': 100,
                'latency': 0,
                'loss': 0,
                'n_clients': 1,
                'n_proxies': 1,
                'show_first_client': True,
                'show_second_client': False,
                'fec': False,
                'low_latency': True,
                'client_start_offset': 0.7,
                'live_latency_goal': 1.2, # We can actually do 1.1
                'enable_tli': False,
                'videos': "[['avatarcmaf8_1']]",
                'extensions': "['.f4s']",
                'representation_ids': "['5']",
                'amount_to_multicast': 1,
                'seg_duration': 4,
                'chunk_count': 8,
                'mc_start_spread': 1,
            },
            '14_hybrid_0_266_sec_ll': {
                'title': '1-1 Hybrid (4 sec, 15 chunks) CMAF',
                'bandwidth_mc': 100,
                'latency': 0,
                'loss': 0,
                'n_clients': 1,
                'n_proxies': 1,
                'show_first_client': True,
                'show_second_client': False,
                'fec': False,
                'low_latency': True,
                'client_start_offset': 0.685,
                'live_latency_goal': 1.0,
                'enable_tli': False,
                'videos': "[['avatarcmaf15_1']]",
                'extensions': "['.f4s']",
                'representation_ids': "['5']",
                'amount_to_multicast': 1,
                'seg_duration': 4,
                'chunk_count': 15,
                'mc_start_spread': 1,
            },
        }

        self.default_metrics_visible = {}
        # Create a dict with with the keys from the default experiments and set the value to False
        for key in self.default_experiments.keys():
            self.default_metrics_visible[key] = False

        self.current_selected_preset = ctk.StringVar(value='01_only_unicast_4_sec')

        self.grid_rowconfigure(0, weight=1)

    def cycle_through_experiment_presets(self, forward = True):
        keys = list(self.default_experiments.keys())
        current_index = keys.index(self.current_selected_preset.get())
        if forward:
            current_index += 1
            if current_index >= len(keys):
                current_index = 0
        else:
            current_index -= 1
            if current_index < 0:
                current_index = len(keys) - 1

        new_preset = keys[current_index]

        self.select_preset_experiment(new_preset)

    def start(self):
        print('Starting experiment frame')

        self.preset_options = ctk.CTkOptionMenu(self.master, command=self.select_preset_experiment, values=list(self.default_experiments.keys()), variable=self.current_selected_preset)
        self.preset_options.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.show_default_metrics_checkbox = CheckBox(self.master, text='Show default metrics', variable=tk.BooleanVar(value=False), action=self.toggle_default_metrics)
        self.show_default_metrics_checkbox.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.hide_all_default_metrics_button = ctk.CTkButton(self.master, text='Hide all default metrics', command=self.hide_all_default_metrics)
        self.hide_all_default_metrics_button.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.seperator0 = ttk.Separator(self.master, orient='horizontal')
        self.seperator0.pack(padx=1, pady=1, anchor='nw', fill='x')

        self.start_stop_button = ctk.CTkButton(self.master, text='Start experiment', command=self.start_experiment, fg_color='green')
        self.start_stop_button.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.open_manage_experiment_button = ctk.CTkButton(self.master, text='Manage past experiments', command=self.open_manage_experiment_popup)
        self.open_manage_experiment_button.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.reset_button = ctk.CTkButton(self.master, text='Remove previous experiments', command=self.remove_all_experiments, fg_color='red')
        self.reset_button.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.seperator1 = ttk.Separator(self.master, orient='horizontal')
        self.seperator1.pack(padx=1, pady=1, anchor='nw', fill='x')

        self.progress_bar = ctk.CTkProgressBar(self.master, orientation='horizontal', mode='determinate', progress_color='green', height=14)
        self.progress_bar.pack(padx=5, pady=5, anchor='nw', fill='x')
        self.progress_bar.set(1)

        self.seperator2 = ttk.Separator(self.master, orient='horizontal')
        self.seperator2.pack(padx=1, pady=1, anchor='nw', fill='x')

        self.bandwidth_spinbox = IntSpinbox(self.master, width=100, height=32, step_size=1, text='Bandwidth (Mbps)', min_value=0, max_value=100)
        self.bandwidth_spinbox.pack(padx=5, pady=5, anchor='nw', fill='x')
        self.bandwidth_spinbox.set(25)

        self.latency_spinbox = IntSpinbox(self.master, width=100, height=32, step_size=1, text='Latency (ms)', min_value=0, max_value=100)
        self.latency_spinbox.pack(padx=5, pady=5, anchor='nw', fill='x')
        self.latency_spinbox.set(0)

        self.loss_spinbox = IntSpinbox(self.master, width=100, height=32, step_size=1, text='Loss (%)', min_value=0, max_value=100)
        self.loss_spinbox.pack(padx=5, pady=5, anchor='nw', fill='x')
        self.loss_spinbox.set(0)

        self.update_network_button = ctk.CTkButton(self.master, text='Update network', command=self.update_network)
        self.update_network_button.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.seperator3 = ttk.Separator(self.master, orient='horizontal')
        self.seperator3.pack(padx=1, pady=1, anchor='nw', fill='x')

        self.n_clients_spinbox = IntSpinbox(self.master, width=100, height=32, step_size=1, text='Clients per proxy', min_value=0, max_value=5)
        self.n_clients_spinbox.pack(padx=5, pady=5, anchor='nw', fill='x')
        self.n_clients_spinbox.set(1)

        self.n_proxies_spinbox = IntSpinbox(self.master, width=100, height=32, step_size=1, text='Proxies', min_value=0, max_value=5)
        self.n_proxies_spinbox.pack(padx=5, pady=5, anchor='nw', fill='x')
        self.n_proxies_spinbox.set(1)

        self.seperator4 = ttk.Separator(self.master, orient='horizontal')
        self.seperator4.pack(padx=1, pady=1, anchor='nw', fill='x')
        
        self.show_first_client_checkbox = CheckBox(self.master, text='Show first client', variable=tk.BooleanVar(value=True))
        self.show_first_client_checkbox.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.show_second_client_checkbox = CheckBox(self.master, text='Show second client', variable=tk.BooleanVar(value=False))
        self.show_second_client_checkbox.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.seperator5 = ttk.Separator(self.master, orient='horizontal')
        self.seperator5.pack(padx=1, pady=1, anchor='nw', fill='x')

        self.fec_checkbox = CheckBox(self.master, text='Enable FEC', variable=tk.BooleanVar(value=False))
        self.fec_checkbox.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.enable_tli_checkbox = CheckBox(self.master, text='Enable TLI', variable=tk.BooleanVar(value=True))
        self.enable_tli_checkbox.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.low_latency_checkbox = CheckBox(self.master, text='Enable low-latency', variable=tk.BooleanVar(value=True))
        self.low_latency_checkbox.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.client_start_offset = FloatSpinbox(self.master, width=100, height=32, step_size=0.5, text='Client start offset (s)', min_value=0, max_value=10)
        self.client_start_offset.pack(padx=5, pady=5, anchor='nw', fill='x')
        self.client_start_offset.set(2)

        self.live_latency_goal = FloatSpinbox(self.master, width=100, height=32, step_size=0.5, text='Live latency goal (s)', min_value=0, max_value=10)
        self.live_latency_goal.pack(padx=5, pady=5, anchor='nw', fill='x')
        self.live_latency_goal.set(6.5)

        self.seperator6 = ttk.Separator(self.master, orient='horizontal')
        self.seperator6.pack(padx=1, pady=1, anchor='nw', fill='x')

        self.videos_field = TextInputBox(self.master, width=100, height=32, text='Videos', default_text="[['avatar_midhigh_1']]")
        self.videos_field.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.extensions_field = TextInputBox(self.master, width=100, height=32, text='Extensions', default_text="['.mp4']")
        self.extensions_field.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.representation_ids_field = TextInputBox(self.master, width=100, height=32, text='Representation IDs', default_text="['4']")
        self.representation_ids_field.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.seg_duration_spinbox = IntSpinbox(self.master, width=100, height=32, step_size=1, text='Segment duration (s)', min_value=1, max_value=4)
        self.seg_duration_spinbox.pack(padx=5, pady=5, anchor='nw', fill='x')
        self.seg_duration_spinbox.set(4)

        self.chunk_count_spinbox = IntSpinbox(self.master, width=100, height=32, step_size=1, text='Chunk count', min_value=1, max_value=1024)
        self.chunk_count_spinbox.pack(padx=5, pady=5, anchor='nw', fill='x')
        self.chunk_count_spinbox.set(1)

        self.amount_to_multicast_field = IntSpinbox(self.master, width=100, height=32, step_size=1, text='Amount to multicast', min_value=0, max_value=25)
        self.amount_to_multicast_field.pack(padx=5, pady=5, anchor='nw', fill='x')
        self.amount_to_multicast_field.set(1)

        self.mc_start_spread_spinbox = IntSpinbox(self.master, width=100, height=32, step_size=1, text='Video start spread (s)', min_value=0, max_value=10)
        self.mc_start_spread_spinbox.pack(padx=5, pady=5, anchor='nw', fill='x')
        self.mc_start_spread_spinbox.set(1)

        self.seperator7 = ttk.Separator(self.master, orient='horizontal')
        self.seperator7.pack(padx=1, pady=1, anchor='nw', fill='x')

        self.open_individual_graph_button = ctk.CTkButton(self.master, text='Open individual graph', command=self.open_individual_graph)
        self.open_individual_graph_button.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.open_player_button = ctk.CTkButton(self.master, text='Open side-by side-player', command=self.open_side_by_side_player)
        self.open_player_button.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.sync_y_scale_checkbox = CheckBox(self.master, text='Use equal y-scaling', variable=tk.BooleanVar(value=False), action=self.toggle_sync_y_scale)
        self.sync_y_scale_checkbox.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.select_preset_experiment()
        self.master.after(self._interval, self.update_content)

    def update_network(self):
        if not self._current_experiment:
            return
        
        if not self._current_has_booted:
            return
            
        self._metrics.current_bw = self.bandwidth_spinbox.get()
        self._metrics.current_latency = self.latency_spinbox.get()
        self._metrics.current_loss = self.loss_spinbox.get()

        setup.shape_bandwidth(self._metrics.current_bw, self._metrics.current_latency, self._metrics.current_loss)

    def update_window(self, width=None, height=None):
        previous_width, previous_height = self._last_size

        new_width = self.winfo_width() if width is None else width
        if new_width != previous_width:
            self._frame_width = new_width
        new_height = self.winfo_height() if height is None else height
        if new_height != previous_height:
            self._frame_height = new_height

        if (previous_width == self._frame_width and previous_height == self._frame_height):
            self._stabilize_count += 1
            if self._stabilize_count >= self._stabilize_counter:
                self._interval = self._normal_interval  # Revert to normal interval
        else:
            self._stabilize_count = 0
            self._interval = self._fast_interval  # Use faster interval

        self._last_size = (self._frame_width, self._frame_height)

    def update_content(self):
        self.update_window()

        if self._current_experiment:
            if self._current_experiment.completed():
                self.stop_experiment()
            # We only want to update the progress bar if we are using the normal interval
            elif self._interval == self._normal_interval:
                if self._current_experiment.booted() and not self._current_has_booted:
                    self._current_has_booted = True
                    self.progress_bar.set(0)
                    self.progress_bar.configure(determinate_speed=50 / (self._metrics.get_max_items() + 10), progress_color='green')
                self.progress_bar.step()

        self.master.after(self._interval, self.update_content)

    def start_experiment(self):
        if self._current_experiment or self.starting_experiment:
            return

        self.starting_experiment = True
        self._current_has_booted = False
        self._current_experiment = Experiment(self.master, self._metrics, 'Live')


        n_subnets = self.n_proxies_spinbox.get()
        n_clients = self.n_clients_spinbox.get() if n_subnets > 0 else 0

        self._metrics.current_bw = self.bandwidth_spinbox.get()
        self._metrics.current_latency = self.latency_spinbox.get()
        self._metrics.current_loss = self.loss_spinbox.get()
        bw = self._metrics.current_bw
        latency = self._metrics.current_latency
        loss = self._metrics.current_loss

        fec = 1 if self.fec_checkbox.get() else 0
        low_latency = self.low_latency_checkbox.get()
        enable_tli = self.enable_tli_checkbox.get()

        seg_duration = self.seg_duration_spinbox.get()
        chunk_count = self.chunk_count_spinbox.get()

        print(self.videos_field.get())
        print(convert_to_list(self.videos_field.get())[0], [])

        rep_ids = convert_to_list(self.representation_ids_field.get(), [])
        rep_extensions = convert_to_list(self.extensions_field.get(), [])
        videos = convert_to_list(self.videos_field.get(), [])

        min_sleep_time = 5
        extra_client_sleep_time = self.client_start_offset.get()
        live_latency_goal = max(0, self.live_latency_goal.get() - extra_client_sleep_time)

        multicast_sender_sleep_time = []
        count = 0
        mc_start_spread = self.mc_start_spread_spinbox.get()
        sleep_dict = {}
        for video in videos:
            if isinstance(video, list):
                l = []
                for v in video:
                    st = min_sleep_time + (mc_start_spread * count) % seg_duration if v not in sleep_dict else sleep_dict[v]
                    sleep_dict[v] = st
                    count += 1
                    l.append(st)
                multicast_sender_sleep_time.append(l)
            else:
                st = min_sleep_time + (mc_start_spread * count) % seg_duration if video not in sleep_dict else sleep_dict[video]
                sleep_dict[video] = st
                count += 1
                multicast_sender_sleep_time.append(st)

        # Stringify multicast_sender_sleep_time
        for i, mc_sleep_time in enumerate(multicast_sender_sleep_time):
            if isinstance(mc_sleep_time, list):
                multicast_sender_sleep_time[i] = list(map(str, mc_sleep_time))
            else:
                multicast_sender_sleep_time[i] = str(mc_sleep_time)
        
        mc_boot_time = None
        clients_sleep_time = []
        # Enumerate the multicast sender sleep time and add 1 second to the clients sleep time if low-latency is enabled
        for i, mc_sleep_time in enumerate(multicast_sender_sleep_time):
            # Check if it is another list
            if isinstance(mc_sleep_time, list):
                clients_sleep_time.append([])
                for j, sleep_time in enumerate(mc_sleep_time):
                    clients_sleep_time[i].append(int(sleep_time) + extra_client_sleep_time)
                    if mc_boot_time is None:
                        mc_boot_time = int(sleep_time)
            else:
                clients_sleep_time.append(int(mc_sleep_time) + extra_client_sleep_time)
                if mc_boot_time is None:
                    mc_boot_time = int(mc_sleep_time)

        if mc_boot_time is None:
            mc_boot_time = 0
        

        show_clients = show_clients = [[False for _ in range(n_clients)] for _ in range(n_subnets)]
        if n_subnets > 0 and n_clients > 0:
            show_clients[0][0] = self.show_first_client_checkbox.get()
            if n_clients > 1:
                show_clients[0][1] = self.show_second_client_checkbox.get()
            elif n_subnets > 1:
                show_clients[1][0] = self.show_second_client_checkbox.get()


        enable_auto_clients = n_clients > 0
        enable_CLI = False
        wait_for_clients_to_finish = False
        wait_for_multicast_to_finish = False
        amount_to_multicast = self.amount_to_multicast_field.get()

        boot_time = BOOT_TIME_MININET_LONG if n_clients * n_subnets > 15 else BOOT_TIME_MININET

        self.progress_bar.configure(determinate_speed=50 // (boot_time + mc_boot_time), progress_color='#ff9800')

        self._current_experiment.start(
            mc_boot_time=mc_boot_time,
            clients_time_offset=extra_client_sleep_time,
            n_clients=n_clients,
            n_subnets=n_subnets,
            multicast_sender_sleep_time=multicast_sender_sleep_time,
            clients_sleep_time=clients_sleep_time,
            bandwidth=bw,
            latency=latency,
            loss=loss,
            fec=fec,
            seg_dur=seg_duration,
            low_latency=low_latency,
            live_latency=live_latency_goal,
            enable_tli=enable_tli,
            chunk_count=chunk_count,
            rep_ids=rep_ids,
            rep_extensions=rep_extensions,
            videos=videos,
            show_clients=show_clients,
            amount_to_multicast=amount_to_multicast,
            enable_auto_clients=enable_auto_clients,
            enable_CLI=enable_CLI,
            wait_for_clients_to_finish=wait_for_clients_to_finish,
            wait_for_multicast_to_finish=wait_for_multicast_to_finish)
        self.start_stop_button.configure(text='Stop experiment', command=self.stop_experiment, fg_color='red')
        self.progress_bar.set(0)

        self.starting_experiment = False

    def stop_experiment(self):
        if not self._current_experiment:
            return
        # Disable the start/stop button
        self.start_stop_button.configure(state='disabled')
        self._current_experiment.stop()

        # Step 1: Generate the initial name
        base_name = 'Experiment ' + str(len(self.past_experiments) + 1)
        final_name = base_name
        # Step 2: Check if the name already exists
        experiment_names = [exp.name for exp in self.past_experiments]  # Assuming past_experiments contains objects with a 'name' attribute
        if final_name in experiment_names:
            # Step 3: Append a random number or the current time in seconds to make the name unique
            unique_suffix = str(int(time.time()))
            final_name = base_name + '_' + unique_suffix
        # Step 4: Rename the current experiment
        self._current_experiment.rename(final_name)

        if self._current_experiment.booted():
            self.past_experiments.append(self._current_experiment)
        self._current_experiment = None
        self._current_has_booted = False
        self.start_stop_button.configure(text='Start experiment', command=self.start_experiment, fg_color='green')
        self.progress_bar.set(1)
        self.start_stop_button.configure(state='normal')

    def remove_experiment(self, experiment_name: str):
        if len(self.past_experiments) == 0:
            return
        self._metrics.remove_experiment(experiment_name)
        # Filter out the experiment with the given name
        self.past_experiments = [exp for exp in self.past_experiments if exp.name() != experiment_name]

    def remove_all_experiments(self):
        # self.stop_experiment()

        for experiment in self.past_experiments:
            self._metrics.remove_experiment(experiment.name())
        self.past_experiments = []

    def rename_experiment(self, previous_name: str, new_name: str):
        for experiment in self.past_experiments:
            if experiment.name() == previous_name:
                experiment.rename(new_name)
                # Incase there are multiple experiments with the same name, we only rename the first one
                # This allows us to recover, if we accidentally rename an experiment to the same name as another
                break

    def get_experiment_names(self):
        return [experiment.name() for experiment in self.past_experiments]

    def toggle_default_metrics(self):
        # Get the current preset value
        preset = self.current_selected_preset.get()
        if preset not in self.default_experiments:
            print(f'Preset {preset} not found')
            return

        # print(f'Toggling default metrics for {preset}')

        previous_value = self.default_metrics_visible[preset]

        current_value = self.show_default_metrics_checkbox.get()
        
        if previous_value == current_value:
            # The value did not change, so we don't need to do anything
            return

        self.default_metrics_visible[preset] = current_value

        if self.show_default_metrics_checkbox.get():
            print(f'Loading metrics for {preset}')
            self.load_metrics(preset)
        else:
            print(f'Removing metrics for {preset}')
            title = preset
            if preset in self.default_experiments:
                default_exp = self.default_experiments[preset]
                if 'title' in default_exp:
                    title = default_exp['title']
            self._metrics.remove_experiment(title)


    def toggle_sync_y_scale(self):
        # Get the current value
        current_value = self.sync_y_scale_checkbox.get()
        self._metrics.enable_max_y_group_values = current_value

        self._metrics.reset_group_max_y_values()

        self.app.update_graphs()
        # We do this twice because the first time it will not update the y-scale of all the graphs
        self.app.update_graphs()



    def hide_all_default_metrics(self):
        print('Hiding all default metrics')
        for key in self.default_metrics_visible:
            if self.default_metrics_visible[key]:
                self.default_metrics_visible[key] = False
                default_exp = self.default_experiments[key]
                title = default_exp['title'] if 'title' in default_exp else key
                self._metrics.remove_experiment(title)
        self.show_default_metrics_checkbox.set(False)

    def save_metrics(self, experiment_name):
        # Search for the experiment with the given name
        exp = None
        for experiment in self.past_experiments:
            if experiment.name() == experiment_name:
                exp = experiment
                break

        if exp is None:
            return

        print(f'Saving metrics for {experiment_name}')

        metrics_dict = exp.metrics_dict()
        experiment_name = exp.name().replace(' ', '_')
        # Save the metrics to a file
        with open(f'metrics/save_{experiment_name}.metrics.storage', 'w') as f:
            for key, value in metrics_dict.items():
                f.write(f'{key};{value}\n')

    def save_metrics_all(self):
        for experiment in self.past_experiments:
            self.save_metrics(experiment.name())

    def load_metrics(self, experiment_name):
        # Remove the current metrics
        self._metrics.remove_experiment(experiment_name)

        title = experiment_name
        if experiment_name in self.default_experiments:
            default_exp = self.default_experiments[experiment_name]
            if 'title' in default_exp:
                title = default_exp['title']

        # Load the metrics from a file
        try:
            with open(f'metrics/{experiment_name}.metrics.storage', 'r') as f:
                for line in f:
                    parts = line.split(';')
                    if len(parts) >= 2:
                        # the value is the last part
                        value = convert_to_list(parts[-1], [])
                        # All the other parts are part of the key
                        key = ";".join(parts[:-1])
                        # Check if the key is in the default metrics info
                        # If not, we will not show a graph, thus we don't need to add it
                        if key in self._metrics.metrics_info:
                            self._metrics.add(title, key, value)
        except FileNotFoundError:
            print(f"File 'metrics/{experiment_name}.metrics.storage' not found.")

        if self.sync_y_scale_checkbox.get():
            self.app.update_graphs()
            # We do this twice because the first time it will not update the y-scale of all the graphs
            self.app.update_graphs()

    def select_preset_experiment(self, value=None):
        if value is not None:
            self.current_selected_preset.set(value)
        else:
            value = self.current_selected_preset.get()

        if value not in self.default_experiments:
            return

        self.show_default_metrics_checkbox.set(self.default_metrics_visible[value])

        exp = self.default_experiments[value]
    
        if 'bandwidth_mc' in exp:
            self.bandwidth_spinbox.set(exp['bandwidth_mc'])
        if 'latency' in exp:
            self.latency_spinbox.set(exp['latency'])
        if 'loss' in exp:
            self.loss_spinbox.set(exp['loss'])
        if 'n_clients' in exp:
            self.n_clients_spinbox.set(exp['n_clients'])
        if 'n_proxies' in exp:
            self.n_proxies_spinbox.set(exp['n_proxies'])
        if 'show_first_client' in exp:
            self.show_first_client_checkbox.set(exp['show_first_client'])
        if 'show_second_client' in exp:
            self.show_second_client_checkbox.set(exp['show_second_client'])
        if 'fec' in exp:
            self.fec_checkbox.set(exp['fec'])
        if 'low_latency' in exp:
            self.low_latency_checkbox.set(exp['low_latency'])
        if 'client_start_offset' in exp:
            self.client_start_offset.set(exp['client_start_offset'])
        if 'live_latency_goal' in exp:
            self.live_latency_goal.set(exp['live_latency_goal'])
        if 'enable_tli' in exp:
            self.enable_tli_checkbox.set(exp['enable_tli'])
        if 'videos' in exp:
            self.videos_field.set(exp['videos'])
        if 'extensions' in exp:
            self.extensions_field.set(exp['extensions'])
        if 'representation_ids' in exp:
            self.representation_ids_field.set(exp['representation_ids'])
        if 'amount_to_multicast' in exp:
            self.amount_to_multicast_field.set(exp['amount_to_multicast'])
        if 'seg_duration' in exp:
            self.seg_duration_spinbox.set(exp['seg_duration'])
        if 'chunk_count' in exp:
            self.chunk_count_spinbox.set(exp['chunk_count'])
        if 'mc_start_spread' in exp:
            self.mc_start_spread_spinbox.set(exp['mc_start_spread'])

    def open_side_by_side_player(self):
        self.app.open_side_by_side_player()

    def open_individual_graph(self):
        self.app.open_individual_graph()

    def open_manage_experiment_popup(self):
        self.app.open_manage_experiment()

class MetricsFrame(Frame):
    def __init__(self, master, metrics, **kwargs):
        super().__init__(master, metrics, **kwargs)
        self._graphs = []
        self._fast_interval = 10  # Temporary faster interval
        self._normal_interval = self._interval  # The normal interval
        self._stabilize_counter = 5  # Number of stable updates before reverting to normal interval
        self._stabilize_count = 0
        self._last_size = (0, 0)
        self._num_columns = 3
        self._frame_width = 600
        self._frame_height = 1000

    def start(self):
        print('Starting metrics frame')

        metric_names = self._metrics.get_metric_names()
        num_metrics = len(metric_names)
        num_columns = self._num_columns
        num_rows = math.ceil(num_metrics / num_columns)

        print(f'Creating {num_metrics} graphs with {num_columns} columns and {num_rows} rows')

        plot_window = self.update_window()
        plot_width, plot_height = plot_window

        # Add the graphs for all the metrics
        for i, metric_name in enumerate(metric_names):
            row = i // num_columns
            column = i % num_columns
            self._add_graph(metric_name, row=row, column=column, padx=4, pady=4, width=plot_width, height=plot_height)

        self.master.after(self._interval, self.update_content)

    def update_window(self, width = None, height = None, redraw = False):
        num_metrics = len(self._metrics.get_metric_names())
        num_rows = math.ceil(num_metrics / self._num_columns)
        
        previous_width, previous_height = self._last_size

        self._frame_width = self.winfo_width() if width is None else width
        if self._frame_width < 600:
            self._frame_width = 600
        self._frame_height = self.winfo_height() if height is None else height
        if self._frame_height < 1000:
            self._frame_height = 1000


        plot_width = math.floor(self._frame_width / self._num_columns) - 4
        plot_height = math.floor(self._frame_height / num_rows) - 16

        plot_width = max(200, plot_width)
        plot_height = max(300, plot_height)

        if (previous_width == self._frame_width and previous_height == self._frame_height and not redraw):
            self._stabilize_count += 1
            if self._stabilize_count >= self._stabilize_counter:
                self._interval = self._normal_interval  # Revert to normal interval
            return (plot_width, plot_height)
        else:
            self._stabilize_count = 0
            self._interval = self._fast_interval  # Use faster interval

        self._last_size = (self._frame_width, self._frame_height)

        for graph in self._graphs:
            graph.update_window(plot_width, plot_height, redraw)

        return (plot_width, plot_height)

    def update_graphs(self, force_update = False):
        for graph in self._graphs:
            graph.update_content(force_update)

    def update_content(self, force_update = False):
        self.update_graphs(force_update)
        self.update()
        self.master.after(self._interval - 1, self.update_window)
        self.master.after(self._interval, self.update_content)

    def _add_graph(self, metric_name, **kwargs):
        # print(f'Adding graph for {metric_name}')
        metric = self._metrics.get(metric_name)
        graph = Graph(self.master, metric, metrics=self._metrics, **kwargs)
        self._graphs.append(graph)

class App(ctk.CTk):
    def __init__(self, fg_color=None, **kwargs):
        super().__init__(fg_color=fg_color, **kwargs)
        self._metrics = Metrics(max_items=91)
        self._side_by_side_player_popup = None
        self._individual_graph_popup = None
        self._manage_experiment_popup = None
        self._quit_count = 0

        self.title('5G Broadcast demo')
        self.geometry('1600x1000')

        self.grid_columnconfigure(0, weight=1)
        self.grid_columnconfigure(1, weight=4)
        self.grid_rowconfigure(0, weight=1)
        
        self._experiment_frame = ExperimentFrame(self, self._metrics, label_text='Experiments', width=300, height=1000)
        self._experiment_frame.grid(row=0, column=0, padx=5, pady=5, sticky='nsew')
    
        self._metrics_frame = MetricsFrame(self, self._metrics, label_text='Metrics', width=600, height=1000)
        self._metrics_frame.grid(row=0, column=1, padx=5, pady=5, sticky='nsew')

        self.protocol("WM_DELETE_WINDOW", self.quit_app)
        self.bind('<Control-q>', lambda e: self.quit_app())
        self.bind('<Control-r>', lambda e: self.start_experiment())
        self.bind('<Control-e>', lambda e: self.stop_experiment())
        self.bind('<Control-Up>', lambda e: self.select_previous_preset())
        self.bind('<Control-Down>', lambda e: self.select_next_preset())
        # Ctrl + s to save metrics
        self.bind('<Control-s>', lambda e: self._experiment_frame.save_metrics_all())

        self.after(500, self._start)

    def _start(self):
        self._experiment_frame.start()
        self._metrics_frame.start()

        # Scroll down to the bottom of the experiments frame
        #self._experiment_frame.

        self.after(1000, self._metrics_frame.update_window)

    def start_experiment(self):
        if self._experiment_frame:
            self._experiment_frame.start_experiment()
            
    def stop_experiment(self):
        if self._experiment_frame:
            self._experiment_frame.stop_experiment()

    def select_previous_preset(self):
        if self._experiment_frame:
            self._experiment_frame.cycle_through_experiment_presets(forward=False)

    def select_next_preset(self):
        if self._experiment_frame:
            self._experiment_frame.cycle_through_experiment_presets(forward=True)

    def update_graphs(self):
        #self._metrics.set_all_to_updated()
        if self._metrics_frame:
            self._metrics_frame.update_graphs(True)

    def quit_app(self, **kwargs):
        if self._quit_count > 2:
            print('Force quitting')
            exit(0)
        self._quit_count += 1
        self.stop_experiment()
        self.quit()

    def open_side_by_side_player(self):
        if self._side_by_side_player_popup is None or not self._side_by_side_player_popup.winfo_exists():
            self._side_by_side_player_popup = OpenSideBySidePlayerPopup(self)
        else:
            # Check if the window is withdrawn or minimized and deiconify if necessary
            if self._side_by_side_player_popup.state() in ('withdrawn', 'iconic'):
                self._side_by_side_player_popup.deiconify()

            self._side_by_side_player_popup.lift()
            self._side_by_side_player_popup.focus()

    def open_individual_graph(self):
        if self._individual_graph_popup is None or not self._individual_graph_popup.winfo_exists():
            self._individual_graph_popup = OpenIndividualGraphPopup(self, self._metrics)
        else:
            # Check if the window is withdrawn or minimized and deiconify if necessary
            if self._individual_graph_popup.state() in ('withdrawn', 'iconic'):
                self._individual_graph_popup.deiconify()

            self._individual_graph_popup.update_metrics()
            self._individual_graph_popup.lift()
            self._individual_graph_popup.focus()

    def open_manage_experiment(self):
        if self._manage_experiment_popup is None or not self._manage_experiment_popup.winfo_exists():
            self._manage_experiment_popup = OpenManageExperimentPopup(self, self._experiment_frame)
        else:
            # Check if the window is withdrawn or minimized and deiconify if necessary
            if self._manage_experiment_popup.state() in ('withdrawn', 'iconic'):
                self._manage_experiment_popup.deiconify()

            self._manage_experiment_popup.update_experiments()
            self._manage_experiment_popup.lift()
            self._manage_experiment_popup.focus()

class OpenSideBySidePlayerPopup(ctk.CTkToplevel):
    def __init__(self, master, **kwargs):
        super().__init__(master, **kwargs)
        self.title('Side-by-side-player')
        
        # Add a bit of text with a description
        self.description_label = ctk.CTkLabel(self, text='Select the video quality representations to play side by side.')
        self.description_label.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.video_selection1_spinbox = IntSpinbox(self, width=100, height=32, step_size=1, text='Video 1', min_value=1, max_value=5)
        self.video_selection1_spinbox.pack(padx=5, pady=5, anchor='nw', fill='x')
        self.video_selection1_spinbox.set(1)

        self.video_selection2_spinbox = IntSpinbox(self, width=100, height=32, step_size=1, text='Video 2', min_value=1, max_value=5)
        self.video_selection2_spinbox.pack(padx=5, pady=5, anchor='nw', fill='x')
        self.video_selection2_spinbox.set(2)

        self.description_label = ctk.CTkLabel(self, text='Provide the directory where the videos are stored. They should be stored in the format <representationId>.mp4')
        self.description_label.pack(padx=5, pady=5, anchor='nw', fill='x')
        self.video_dir_field = TextInputBox(self, width=100, height=32, text='Video directory', default_text="../server/content/avatar/full/")
        self.video_dir_field.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.start_player_button = ctk.CTkButton(self, text='Start player', command=self.start_player)
        self.start_player_button.pack(padx=5, pady=5, anchor='nw', fill='x')

    def start_player(self):
        video1 = self.video_selection1_spinbox.get()
        video2 = self.video_selection2_spinbox.get()
        print(f'Starting side by side player with video {video1} and {video2}')

        default_dir = self.video_dir_field.get()

        # Start the side by side player
        subprocess.Popen(['./run_simultaneously.sh', f"{default_dir}{video1}.mp4", f"{default_dir}{video2}.mp4"], user=os.getlogin())

        self.withdraw()

class OpenIndividualGraphPopup(ctk.CTkToplevel):
    def __init__(self, master, metrics, **kwargs):
        super().__init__(master, **kwargs)
        self.title('Individual graphs')
        self._metrics = metrics
        self._graphs = []

        # Add a dropdown with all the metrics
        print(list(self._metrics.get_metric_names()))
        self.metric_selection = ctk.CTkOptionMenu(self, values=list(self._metrics.get_metric_names()))
        self.metric_selection.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.load_graph_button = ctk.CTkButton(self, text='Load graph', command=self.load_graph)
        self.load_graph_button.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.refresh_graphs_button = ctk.CTkButton(self, text='Refresh graphs', command=self.update_content)
        self.refresh_graphs_button.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.remove_graph_button = ctk.CTkButton(self, text='Remove graph', command=self.remove_graph, fg_color='red')
        self.remove_graph_button.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.graph_frame = Frame(self, metrics=self._metrics, label_text='Graph', width=1000, height=1000)
        self.graph_frame.pack(padx=5, pady=5, anchor='nw', fill='x')

    def update_metrics(self):
        self.metric_selection.configure(values=list(self._metrics.get_metric_names()))

    def update_content(self):
        for graph in self._graphs:
            graph.update_content()
        self.update()

    def load_graph(self):
        self.select_metric(self.metric_selection.get())

    def remove_graph(self):
        self.remove_metric(self.metric_selection.get())

    def select_metric(self, value):
        # print(f'Selected metric {value}')
        # Check if the graph is already created
        if value in [graph.metric.name() for graph in self._graphs]:
            self.update_content()
            return

        metric = self._metrics.get(value)
        self._graphs.append(Graph(self.graph_frame, metric))

    def remove_metric(self, value):
        graph = None
        for g in self._graphs:
            if g.metric.name() == value:
                graph = g
                break
        
        if graph is None:
            return

        # print(f'Removing graph for {value}', graph)

        graph.destroy()
        self._graphs.remove(graph)

class OpenManageExperimentPopup(ctk.CTkToplevel):
    def __init__(self, master, experiment_frame, **kwargs):
        super().__init__(master, **kwargs)
        self.title('Manage past experiments')
        self._experiment_frame = experiment_frame

        # Add a dropdown with all the metrics
        self.experiment_selection = ctk.CTkOptionMenu(self, values=self.get_experiment_names())
        self.experiment_selection.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.new_name_field = TextInputBox(self, width=100, height=32, text='New name', default_text="")
        self.new_name_field.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.update_name_button = ctk.CTkButton(self, text='Update name', command=self.rename_experiment)
        self.update_name_button.pack(padx=5, pady=5, anchor='nw', fill='x')

        self.remove_experiment_button = ctk.CTkButton(self, text='Remove experiment', command=self.remove_experiment, fg_color='red')
        self.remove_experiment_button.pack(padx=5, pady=5, anchor='nw', fill='x')

    def get_experiment_names(self):
        l = self._experiment_frame.get_experiment_names()
        if len(l) == 0:
            return ['====== No manual experiments ======']
        
        # Append at the beginning of the list
        l.insert(0, '====== Select experiment to manage ======')
        
        return l

    def update_experiments(self):
        self.experiment_selection.configure(values=self.get_experiment_names())

        # Select the first item
        self.experiment_selection.set(self.experiment_selection.cget('values')[0])


    def rename_experiment(self):
        previous_experiment_name = self.experiment_selection.get()

        new_experiment_name = self.new_name_field.get()
        if len(new_experiment_name) == 0:
            return

        self._experiment_frame.rename_experiment(previous_experiment_name, new_experiment_name)

        self.update_experiments()

    def remove_experiment(self):
        selected_experiment = self.experiment_selection.get()
        self._experiment_frame.remove_experiment(selected_experiment)

        self.update_experiments()



def main():
    ctk.set_appearance_mode('light')
    ctk.set_widget_scaling(0.75)
    app = App()

    def signal_handler(sig, frame):
        print('Caught signal')
        app.quit_app()

    signal.signal(signal.SIGINT, signal_handler)

    app.mainloop()

if __name__ == '__main__':
    if os.geteuid() != 0:
        exit("This script needs to run in root privileges!")

    setup.setLogLevel('info')
    main()
