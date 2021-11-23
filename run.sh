#!/usr/bin/env bash
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_OPENSSL=ON -DBUILD_VERSION=1.0.0 ../
make thinger_monitor
if [ $THINGER_TOKEN ]; then
    THINGER_TOKEN="-t $THINGER_TOKEN"
fi
if [ $USER_ID ]; then
    USER_ID="-u $USER_ID"
fi
#SSL_CERT_DIR=/etc/ssl/certs ./thinger_monitor -c "../config/thinger_monitor.json" $THINGER_TOKEN $USER_ID -s "192.168.1.16"
#SSL_CERT_DIR=/etc/ssl/certs ./thinger_monitor -c "../config/thinger_monitor.json" $THINGER_TOKEN -s "192.168.1.16" -k
SSL_CERT_DIR=/etc/ssl/certs ./thinger_monitor -c "../config/thinger_monitor.json" -s "192.168.1.16" -k
