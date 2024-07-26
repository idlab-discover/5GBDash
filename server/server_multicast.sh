#!/bin/bash

debug=false


if [ "$debug" = true ]; then
    # Use this in case of debugging using gdb
    PROGRAM="python3 -u server_multicast.py $@"
    # GDB command
    GDB_COMMAND="gdb --args $PROGRAM"
    # Output file
    OUTPUT_FILE="gdb_output_$id.txt"
    # GDB commands to handle SIGSEGV
    GDB_SIGNAL_HANDLING="" #"handle SIGSEGV nostop"
    # Combine GDB commands
    GDB_COMMANDS="$GDB_SIGNAL_HANDLING\nrun\nbt\nquit"
    # Run the program with GDB and redirect output to a file
    echo -e "$GDB_COMMANDS" | $GDB_COMMAND
else
    python3 -u server_multicast.py "$@"
fi