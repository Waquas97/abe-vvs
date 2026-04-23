#!/bin/bash

#requirements
sudo apt-get update
sudo apt-get --assume-yes install libxml2-dev
sudo apt-get --assume-yes install libcurl4-openssl-dev
sudo apt-get --assume-yes install sysstat

####################################################################################
#Pre-requiste For CPABE
sudo apt-get -y install flex bison libssl-dev python-dev-is-python3 libgmp-dev

#PBC lib
wget http://crypto.stanford.edu/pbc/files/pbc-0.5.14.tar.gz
tar xf pbc-0.5.14.tar.gz
cd pbc-0.5.14
./configure && make && sudo make install
cd ..

#pre requiste for libsswabe
sudo apt-get --assume-yes install libglib2.0-dev

#libswabb for cpabetoolkit
wget http://acsc.cs.utexas.edu/cpabe/libbswabe-0.9.tar.gz
tar xf libbswabe-0.9.tar.gz
cd libbswabe-0.9
./configure && make && sudo make install
cd ..

wget http://acsc.cs.utexas.edu/cpabe/cpabe-0.11.tar.gz
tar xf cpabe-0.11.tar.gz
cd cpabe-0.11
./configure 

#fix bugs in makefile and policy_lang.y
sed -i '/-lglib-2.0 \\/a -Wl,--copy-dt-needed-entries \\' Makefile
sed -i '/result: policy { final_policy = $1 }/s/$1 }/$1; }/' policy_lang.y

make && sudo make install
cd ..

#Install PIP to install pandas
sudo apt --assume-yes install python3-pip
pip install pandas

######################################################################################

#get streaming client
wget -L "https://raw.githubusercontent.com/Waquas97/abe-vvs/master/Streaming-Client/streaming-client.tar.xz"
tar -xf streaming-client.tar.xz
rm streaming-client.tar.xz
cd build
make
cd ..
mkdir client-1
cp build/stream_client client-1/
cp build/pub_key client-1
cp build/user_key client-1
cp -r client-1 client-2
cp -r client-1 client-3
cp -r client-1 client-4
cp -r client-1 client-5
cp -r client-1 client-6
cp -r client-1 client-7
cp -r client-1 client-8

