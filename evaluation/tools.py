import pandas as pd
import matplotlib.pyplot as plt
import os
import datetime
import numpy as np

def get_metric_files(iteration):
    # Get a list of all filenames in the directory
    file_names = os.listdir('./logs/' + iteration)
    # Filter on metric files
    return ['logs/' + iteration + '/' + filename for filename in file_names if filename.endswith("metric.log")]

def get_latest_dir(directory = './logs'):
    max_number = float('-inf')
    largest_directory = None

    for item in os.listdir(directory):
        if os.path.isdir(os.path.join(directory, item)):
            try:
                number = int(item)
                if number > max_number:
                    max_number = number
                    largest_directory = item
            except ValueError:
                continue

    return largest_directory

def get_all_experiments(filename = 'experiments.log', interpolate=True, align=False, resample=False):
    experiments_df = pd.read_csv(f'./logs/{filename}', sep=':', header=None, names=['experiment', 'directory'], engine='c')

    # Create an empty dictionary to store the maximum directory value for each experiment ID
    max_directory_dict = {}

    # Iterate through rows in the DataFrame
    for index, row in experiments_df.iterrows():
        # Get experiment ID and directory value from the current row
        experiment_id = str(row['experiment'])
        directory_value = int(row['directory'])

        # Check if experiment ID already exists in the dictionary
        if experiment_id in max_directory_dict:
            # If it does, compare the directory values
            if directory_value > max_directory_dict[experiment_id]:
                # Update the maximum directory value for this experiment ID
                max_directory_dict[experiment_id] = directory_value
        else:
            # If the experiment ID is not in the dictionary, add it with the current directory value
            max_directory_dict[experiment_id] = directory_value

    max_directory_list = [experiment_id + "_,_" + str(max_directory) for experiment_id, max_directory in max_directory_dict.items()]

    # Filter the DataFrame to keep only rows with the maximum directory value for each experiment ID
    experiments_df = experiments_df.loc[
        (experiments_df['experiment'].astype(str) + "_,_" +  experiments_df['directory'].astype(str)).isin(
          max_directory_list  
        )
    ]

    # We use a dict first so we can overwrite experiments with the same id
    experiments_dict = {}
    for index, row in experiments_df.iterrows():        
        directory = str(row.directory)
        dfs = get_all_data(directory, resample=resample, align=align, interpolate=interpolate)
        #start, end = tools.get_start_and_end(dfs)
        #events = tools.get_all_events(dfs)
        info = read_info(directory)
        result = {
            'id': str(row.experiment),
            'directory': directory,
            'info': info,
            'dfs': dfs
        }

        experiments_dict[str(row.experiment)] = result

    experiments = [experiments_dict[key] for key in sorted(experiments_dict.keys())]
    
    return experiments

def read_data(filename):
    try:
        # Try to read the file into a pandas DataFrame
        data = pd.read_csv(filename, sep=';', header=None, engine='c')
        
        # Rename columns if the DataFrame is not empty
        data.columns = ['timestamp', 'event', 'value']
    except pd.errors.EmptyDataError:
        print(f"File {filename} is empty. Creating an empty DataFrame.")
        # If the file is empty, create an empty DataFrame with the specified columns
        data = pd.DataFrame(columns=['timestamp', 'event', 'value'])
        data.columns = ['timestamp', 'event', 'value']
    except pd.errors.ParserError:
        print(f"File {filename} could not be parsed. Creating an empty DataFrame.")
        # If the file is empty, create an empty DataFrame with the specified columns
        data = pd.DataFrame(columns=['timestamp', 'event', 'value'])

    return data

def read_info(iteration):
    data = {}
    with open('./logs/' + iteration + '/info.log', 'r') as file:
        for line in file:
            parts = line.strip().split(':')
            if len(parts) == 2:
                key, value = parts
                data[key.strip()] = value.strip()

    # Check if key 'dir' exists
    if 'dir' not in data:
        # if not, set it using the iteration number
        data['dir'] = iteration

    return data

def parse_df(df):
    # Convert the value column to numeric
    df['value'] = pd.to_numeric(df['value'], errors='coerce')
    # Pivot the DataFrame so each event is a column
    new_df = pd.pivot_table(df, index='timestamp', columns='event', values='value', aggfunc='mean')

    # Sort the DataFrame by timestamp
    new_df.sort_values(by='timestamp', inplace=True)
    new_df = new_df.reset_index()

    # Convert the timestamp column to datetime
    new_df['timestamp'] = new_df['timestamp'] + '000'
    new_df['timestamp'] = pd.to_datetime(new_df['timestamp'], format='%Y-%m-%d %H:%M:%S,%f')
    new_df.set_index('timestamp', inplace=True)

    return new_df


def get_all_data(directory: str, align = False, interpolate = True, resample = False):
    files = get_metric_files(directory)
    files.sort() # Not necessary but nice to have.
    dfs = {}
    begin = ''
    for file in files:
        parts = file.split('/')
        name = parts[-1].split('.')[0]
        data = read_data(file)
        # print(f"Read {len(data)} rows from {file}")
        try:
            dfs[name] = parse_df(data)
        except Exception:
            print(f"Error while parsing {file}")
            dfs[name] = pd.DataFrame(pd.NA, index=range(0), columns=['timestamp'])

    if align:
        start = None
        for df_name, df in dfs.items():
            start = min(start, df.index.min()) if start else df.index.min()

        for df_name, df in dfs.items():
            new_df = pd.DataFrame(pd.NA, index=range(0), columns=['timestamp'])
            new_df.set_index('timestamp', inplace=True)
            temp_df = df.copy()
            temp_df.index = (temp_df.index - start).total_seconds()
            temp_df.index = pd.to_datetime(temp_df.index, unit='s')
            new_df = new_df.merge(temp_df, how='outer', left_index=True, right_index=True)
            new_df = new_df.sort_values(by='timestamp')
            dfs[df_name] = new_df

    for df_name, df in dfs.items():
        # This merges rows with the same timestamp (index).
        df = df.groupby('timestamp').mean(numeric_only=True)

        if resample:
            # Resample to a lower frequency to avoid duplicate timestamps
            df = df.apply(pd.to_numeric, errors='ignore')
            df = df.asfreq(freq='0.001S', fill_value=np.nan)

        if interpolate:
            df = df.apply(pd.to_numeric, errors='ignore')
            # Perform linear interpolation
            df.interpolate(inplace=True)

        dfs[df_name] = df

    return dfs

def get_events(df):
    return df.columns

def get_all_events(dfs):
    events = set()
    for df in dfs.values():
        events.update(get_events(df))
    return list(events)

def get_metrics_by_events(dfs, events: list, interpolate=True):
    columns = set(events)
    dfs_found = {}
    for df_name, df in dfs.items():
        # Check if df is a DataFrame
        if not isinstance(df, pd.DataFrame):
            print(f"Skipping invalid dataframe {df_name}")
            continue
        df_columns = set(df.columns)
        if columns&df_columns:
            selected_columns = list(columns.intersection(df_columns))
            selected_df = df[selected_columns].copy()
            selected_df.columns = [df_name + ' ' + col for col in selected_df.columns]
            dfs_found[df_name] = selected_df

    new_df = pd.DataFrame(pd.NA, index=range(0), columns=['timestamp'])
    new_df.set_index('timestamp', inplace=True)
    for df_name, df in dfs_found.items():
        new_df = new_df.merge(df, how='outer', left_index=True, right_index=True)

    new_df.sort_values(by='timestamp', inplace=True)
    new_df = new_df.groupby('timestamp').mean(numeric_only=True)    

    if interpolate:
        new_df = new_df.apply(pd.to_numeric, errors='ignore')
        # Perform linear interpolation
        new_df.interpolate(inplace=True)

    return new_df

def should_include_default(info):
    # Default implementation, change this as needed
    return True

def get_metrics_by_events_from_experiments(experiments, events: list, resample=None, interpolate=True, align=False, should_include_func=should_include_default,):
    dfs_found = {}
    for experiment in experiments:
        if should_include_func(experiment['info']):
            df = get_metrics_by_events(
                experiment['dfs'],
                events,
                interpolate=(interpolate and not align)
            )
            if df.size > 0:
                df.columns = [experiment['id'] + ' ' + col for col in df.columns]
                dfs_found[experiment['id']] = df

    new_df = pd.DataFrame(pd.NA, index=range(0), columns=['timestamp'])
    new_df.set_index('timestamp', inplace=True)
    for df_name, df in dfs_found.items():
        if align:
            temp_df = df.copy()
            temp_df.index = (temp_df.index - temp_df.index.min()).total_seconds()
            temp_df.index = pd.to_datetime(temp_df.index, unit='s')
            new_df = new_df.merge(temp_df, how='outer', left_index=True, right_index=True)
        else:
            new_df = new_df.merge(df, how='outer', left_index=True, right_index=True)

    new_df = new_df.sort_values(by='timestamp')
    new_df = new_df.groupby('timestamp').mean(numeric_only=True)

    # Convert the index to a DatetimeIndex
    new_df.index = pd.to_datetime(new_df.index)

    if resample:
        # Resample to a lower frequency to avoid duplicate timestamps
        new_df = new_df.resample(resample).mean(numeric_only=True)

    if interpolate and align:
        new_df = new_df.apply(pd.to_numeric, errors='ignore')
        # Perform linear interpolation
        new_df.interpolate(inplace=True)

    return new_df

def plot_data(data,
              start_time=None,
              end_time=None,
              title='Event Counts Over Time',
              legend_title=None,
              xlabel='Timestamp',
              ylabel='Count',
              figsize=(20, 10),
              linewidth=1.5,
              font_size=18,
              linestyle = '-',
              linestyle_fec = '--',
              legend_font_size=15,
              legend_columns=1,
              dpi=300,
              grid=False,
              colors=None,
              bottom_limit=None,
              top_limit=None,
              legend=True,
              sort_legend=True,
              show_fec_legend=True,
              legend_location='upper right'):
    plt.clf()
    if dpi is not None:
        plt.rcParams['figure.dpi'] = dpi  # You can adjust this value as needed
    plt.rcParams['font.family'] = 'Times New Roman'
    if font_size is not None:
        plt.rcParams['font.size'] = font_size
    fig, ax = plt.subplots(figsize=figsize)

    multiple = isinstance(data, list)

    # Check if data is a list of DataFrames
    if (multiple and len(data) == 0) or (not multiple and data.empty):
        print("No data to plot.")
        return

    default_df = data[0] if multiple else data
    if start_time is not None:
        start_time = pd.to_datetime(start_time)
    else:
        start_time = default_df.index.min()

    if end_time is not None:
        end_time = pd.to_datetime(end_time)
    else:
        end_time = default_df.index.max()

    # convert start and end time to seconds
    end_time = (end_time - start_time).total_seconds()
    original_start_time = start_time
    start_time = (start_time - start_time).total_seconds()


    def _plot_df(df, i=0, fec_included=False):
        # Get the colums of the dataframe
        columns = df.columns
        if sort_legend:
            columns = sorted(columns)

        colors_map = plt.cm.get_cmap('tab10')
        # Iterate over colums, and also get the index
        for column in columns:
            has_fec = 'fec' in column or 'FEC' in column
            # Now we search for the color
            color_index = i // 2 if fec_included else i
            color = colors_map(color_index) if colors is None else colors[color_index % len(colors)]

            # Get data that is above 4.5
            #average = data[column].mean()
            #print(f"Average {column}: {average}")
            x_values = (df.index - original_start_time).total_seconds().to_numpy()
            y_values = df[column].to_numpy()
            ax.plot(x_values, y_values, label=column, linewidth=linewidth, linestyle=linestyle_fec if has_fec else linestyle, color=color, alpha=0.5 if has_fec else 1)

    if multiple:
        # Includes fec in at least one column
        fec_included = any(any('fec' in column or 'FEC' in column for column in df.columns) for df in data)
        for i, df in enumerate(data):
            # Check if df is actually a dataframe
            if isinstance(df, pd.DataFrame):
                _plot_df(df, i, fec_included)
            else:
                print("Skipping invalid dataframe")
    else:
        _plot_df(data)

    if bottom_limit is not None:
        if top_limit is not None:
            ax.set_ylim(bottom=bottom_limit, top=top_limit)
        else:
            ax.set_ylim(bottom=bottom_limit)
    elif top_limit is not None:
        ax.set_ylim(top=top_limit)

    ax.set_xlim(start_time, end_time-1)  # Set the x-axis limits based on data range
    ax.set_title(title)
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    if legend:
        # Get handles and labels
        handles, labels = plt.gca().get_legend_handles_labels()
        lables = [label.strip() for label in labels]
        # Replace '.0 %' with ' %' in labels
        labels = [label.replace('.0 %', ' %') for label in labels]
        if not show_fec_legend:
            # Remove FEC labels
            handles = [h for h, l in zip(handles, labels) if 'fec' not in l and 'FEC' not in l]
            labels = [l for l in labels if 'fec' not in l and 'FEC' not in l]
        if sort_legend:
            # Sort handles and labels alphabetically by labels
            handles, labels = zip(*sorted(zip(handles, labels), key=lambda x: x[1]))

        ax.legend(handles, labels, title=legend_title, loc=legend_location, fontsize=legend_font_size, borderpad=0.4, labelspacing=0.2, ncol=legend_columns, columnspacing=1, handletextpad=0.2, framealpha=1, edgecolor='black')
    if grid:
        ax.grid()

    fig.subplots_adjust(hspace=0, wspace=0)

    plt.show()
    
def get_start_and_end(dfs):
    datetime_objects = []
    for df in dfs.values():
        if df.size:
            datetime_objects.append(df.first_valid_index())
            datetime_objects.append(df.last_valid_index())
    if len(datetime_objects) > 0:
        begin = min(datetime_objects) # The first item of all metrics
        end = max(datetime_objects) # The last item of all metrics
        return begin, end
    
    return 0, 1