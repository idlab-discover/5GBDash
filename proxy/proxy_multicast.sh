#!/bin/bash

debug=false
id=$1
recover=$2
video_list=$3


# fdt_retrieval_interval=1000
# alc_retrieval_interval=100

fdt_retrieval_interval=50
alc_retrieval_interval=10

echo "Id: $id"
echo "Recover: $recover"
echo "Video list: $video_list"

# If the length of video list is not 0, then append '-v ' to the beginning of video list
if [ ${#video_list} -ne 0 ]; then
    video_list="-v $video_list"
fi

dir="cache_$id"

mkdir -p $dir

if [ "$debug" = true ]; then
    # Use this in case of debugging using gdb
    PROGRAM="../flute/build/examples/flute_receiver -m 239.0.0.1 -p 16000 -l 0 -d $dir -r $recover -f $fdt_retrieval_interval -a $alc_retrieval_interval $video_list"
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
    ../flute/build/examples/flute_receiver -m 239.0.0.1 -p 16000 -l 2 -d $dir -r $recover -f $fdt_retrieval_interval -a $alc_retrieval_interval $video_list
fi