#!/usr/bin/env sh
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_OPENSSL=ON -DSTATIC=ON -DOPENSSL_ROOT_DIR=/usr/lib/ssl -DOPENSSL_LIBRARIES=/usr/lib/ssl ../
make -j$((`grep -c ^processor /proc/cpuinfo`+1)) thinger_monitor
#./test
#make thinger_monitor
if [ $THINGER_TOKEN ]; then
    THINGER_TOKEN="-t $THINGER_TOKEN"
fi
if [ $USER_ID ]; then
    USER_ID="-u $USER_ID"
fi
SSL_CERT_DIR=/etc/ssl/certs ./thinger_monitor -c "../config/thinger_monitor.json"
