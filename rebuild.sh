#!/bin/bash

if [ "$1" = "ANDROID" ] ; then
    echo "ANDROID rebuild mode"
elif [ "$1" = "IOS" ] ; then
    echo "IOS rebuild mode"
elif [ "$1" = "MSVC" ] ; then
    echo "MSVC rebuild mode"
elif [ "$1" = "OSX" ] ; then
    echo "OSX rebuild mode"
else
    echo "unknown rebuild mode"
    exit 0
fi

./clean.sh
./build.sh $1 $2