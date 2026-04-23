#!/bin/bash

sudo apt update
sudo apt --assume-yes install apache2
sudo apt --assume-yes install openssl
sudo a2enmod ssl
sudo a2ensite default-ssl
sudo ufw allow 'Apache Full'
sudo systemctl restart apache2

sudo apt-get --assume-yes install sysstat




# Dataset for streaming
# Base URL for the files
url="https://raw.githubusercontent.com/Waquas97/abe-vvs/master/"

# 108k-original
wget -L "${url}PointCloud-dataset/108k/108k-stream-original-24fps/108k-00001.ply"
wget -L "${url}PointCloud-dataset/108k/108k-stream-original-24fps/108k-original-24fps.mpd"
wget -L "${url}PointCloud-dataset/108k/108k-stream-original-24fps/cp.sh"
bash cp.sh
rm cp.sh

# 108k-x
wget -L "${url}PointCloud-dataset/108k/108k-stream-x-24fps/108k-x-00001.ply.cpabe"
wget -L "${url}PointCloud-dataset/108k/108k-stream-x-24fps/108k-x-24fps.mpd"
wget -L "${url}PointCloud-dataset/108k/108k-stream-x-24fps/cp.sh"
bash cp.sh
rm cp.sh

# 108k-xy
wget -L "${url}PointCloud-dataset/108k/108k-stream-xy-24fps/108k-xy-00001.ply.cpabe"
wget -L "${url}PointCloud-dataset/108k/108k-stream-xy-24fps/108k-xy-24fps.mpd"
wget -L "${url}PointCloud-dataset/108k/108k-stream-xy-24fps/cp.sh"
bash cp.sh
rm cp.sh

# 108k-xyz
wget -L "${url}PointCloud-dataset/108k/108k-stream-xyz-24fps/108k-xyz-00001.ply.cpabe"
wget -L "${url}PointCloud-dataset/108k/108k-stream-xyz-24fps/108k-xyz-24fps.mpd"
wget -L "${url}PointCloud-dataset/108k/108k-stream-xyz-24fps/cp.sh"
bash cp.sh
rm cp.sh

sudo mv *.ply* /var/www/html/
sudo mv *.mpd* /var/www/html/

