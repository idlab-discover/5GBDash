#!/bin/bash

video1=$1
video2=$2

mpv --lavfi-complex="[vid1][vid2]vstack[vo];[aid1][aid2]amix[ao]" $video1 --external-file=$video2

