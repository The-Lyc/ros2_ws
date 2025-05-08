#!/bin/bash

source ~/ros2_jazzy/install/setup.zsh

for((i=1;i<=40;i++));   do
    echo "execute $i-th"
    ./skipping_under_timermanager "$i"
done
