# For me to copy paste on cache node terminal
#!/bin/bash

sudo apt-get update
sudo apt-get --assume-yes install sysstat
sudo apt-get --assume-yes install -y gcc g++ make libssl-dev tcl libexpat1-dev libpcre3-dev zlib1g-dev libcap-dev libxml2-dev libtool
sudo apt-get --assume-yes install cmake
sudo apt-get --assume-yes install pkg-config
sudo apt-get --assume-yes install libpcre2-8
sudo apt-get --assume-yes install libpcre2-dev
git clone https://git-wip-us.apache.org/repos/asf/trafficserver.git
cd trafficserver/
cmake -B build -DCMAKE_INSTALL_PREFIX=/opt/ts
cmake --build build
cmake --build build -t test
sudo cmake --install build
cd /opt/ts
sudo bin/traffic_server -R 1

cd

wget -L https://raw.githubusercontent.com/Waquas97/abe-vvs/master/experiment-setup/records.yaml
sudo rm /opt/ts/etc/trafficserver/records.yaml
sudo mv records.yaml /opt/ts/etc/trafficserver/ 

#cache-server
echo 'map https://10.10.1.1:443 https://10.10.1.2:443 @plugin=cachekey.so @pparam=--remove-prefix=true' | sudo tee -a /opt/ts/etc/trafficserver/remap.config 
echo 'map http://10.10.1.1:80 http://10.10.1.2:80 @plugin=cachekey.so @pparam=--remove-prefix=true' | sudo tee -a /opt/ts/etc/trafficserver/remap.config 

#cache-client1
echo 'map https://10.10.2.1:443 https://10.10.1.2:443 @plugin=cachekey.so @pparam=--remove-prefix=true' | sudo tee -a /opt/ts/etc/trafficserver/remap.config 
echo 'map http://10.10.2.1:80 http://10.10.1.2:80 @plugin=cachekey.so @pparam=--remove-prefix=true' | sudo tee -a /opt/ts/etc/trafficserver/remap.config 

#cache-client2
echo 'map https://10.10.3.1:443 https://10.10.1.2:443 @plugin=cachekey.so @pparam=--remove-prefix=true' | sudo tee -a /opt/ts/etc/trafficserver/remap.config 
echo 'map http://10.10.3.1:80 http://10.10.1.2:80 @plugin=cachekey.so @pparam=--remove-prefix=true' | sudo tee -a /opt/ts/etc/trafficserver/remap.config 

#cache-client3
echo 'map https://10.10.4.1:443 https://10.10.1.2:443 @plugin=cachekey.so @pparam=--remove-prefix=true' | sudo tee -a /opt/ts/etc/trafficserver/remap.config 
echo 'map http://10.10.4.1:80 http://10.10.1.2:80 @plugin=cachekey.so @pparam=--remove-prefix=true' | sudo tee -a /opt/ts/etc/trafficserver/remap.config 


echo 'dest_ip=* ssl_cert_name=server.crt ssl_key_name=server.key' | sudo tee -a /opt/ts/etc/trafficserver/ssl_multicert.config

sudo /opt/ts/bin/./trafficserver restart


# wget -L https://raw.githubusercontent.com/Waquas97/ABE-Streaming/master/Small-Scale/TC-cache.sh
# bash TC-cache.sh



# Need to do manually
sudo apt-get --assume-yes install wireshark-common


#For ssl cert and key have to do manually:
cd /opt/ts/etc
sudo mkdir ssl
cd ssl
sudo openssl req -new -newkey rsa:2048 -days 365 -nodes -x509 -keyout server.key -out server.crt
sudo chmod 777 *





# # TCP dump for capturing all pkts going in and out of node1 (server) fr
# sudo tcpdump -i $(ifconfig | grep -o 'enp[^:]*') -s 0 -w traffic.pcap host node0-link-1
# #clear tcpdump files
# sudo rm *pcap
# # get total volume from tcpdump file
# capinfos traffic.pcap
     









# Need to do manually
sudo apt-get --assume-yes install wireshark-common


#For ssl cert and key have to do manually:
cd /opt/ts/etc
sudo mkdir ssl
cd ssl
sudo openssl req -new -newkey rsa:2048 -days 365 -nodes -x509 -keyout server.key -out server.crt
sudo chmod 777 *



#NEED CHANGED:

# TCP dump for capturing all pkts going in and out of node1 (server) fr
sudo tcpdump -i $(ifconfig | grep -o 'enp[^:]*') -s 0 -w traffic.pcap host node0-link-1
#clear tcpdump files
sudo rm *pcap
# get total volume from tcpdump file
capinfos traffic.pcap
     



# RESTART DOES NOT CLEAR THE CACHE, IT DOES CLEAR THE LOGS for hit miss and unconfirmed midgress.

#changing size of cache and restarting will clear cache as well

# Set Disc cache size
# size 100
sudo sed -i '/^var\/trafficserver/c\var/trafficserver 100M' /opt/ts/etc/trafficserver/storage.config
sudo /opt/ts/bin/./trafficserver restart
#size 250MB
sudo sed -i '/^var\/trafficserver/c\var/trafficserver 250M' /opt/ts/etc/trafficserver/storage.config
sudo /opt/ts/bin/./trafficserver restart
#size 500MB
sudo sed -i '/^var\/trafficserver/c\var/trafficserver 500M' /opt/ts/etc/trafficserver/storage.config
sudo /opt/ts/bin/./trafficserver restart

#  Ram cache is already set 0 in records.yaml
