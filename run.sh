#!/usr/bin/env bash
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_OPENSSL=ON -DBUILD_VERSION=0.0.1 ../
make thinger_monitor
if [ $THINGER_TOKEN ]; then
    THINGER_TOKEN="-t $THINGER_TOKEN"
fi
if [ $USER_ID ]; then
    USER_ID="-u $USER_ID"
fi
SSL_CERT_DIR=/etc/ssl/certs ./thinger_monitor -c "../config/thinger_monitor.json"
