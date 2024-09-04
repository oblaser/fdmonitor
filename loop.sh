#!/bin/bash

# author        Oliver Blaser
# date          03.09.2024
# copyright     GPL-3.0 - Copyright (c) 2024 Oliver Blaser



errCnt=0

./fdmonitor $1
if [ $? -ne 0 ]; then ((++errCnt)); fi;

while [ $errCnt -eq 0 ]
do
    sleep 1
    #clear

    ./fdmonitor $1
    if [ $? -ne 0 ]; then ((++errCnt)); fi;
done
