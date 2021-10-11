#!/usr/bin/env bash
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DDAEMON=OFF ../
make thinger_monitor
if [ $THINGER_TOKEN ]; then
    THINGER_TOKEN="-t $THINGER_TOKEN"
fi
if [ $USER_ID ]; then
    USER_ID="-u $USER_ID"
fi
./thinger_monitor $THINGER_TOKEN $USER_ID
