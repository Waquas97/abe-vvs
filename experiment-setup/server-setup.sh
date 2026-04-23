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


# office52-xyz
wget -L "${url}PointCloud-dataset/office52-enc-xyz-24fps-noattr/office52-xyz-00001-100.ply.cpabe"
wget -L "${url}PointCloud-dataset/office52-enc-xyz-24fps-noattr/office52-xyz-00001-50.ply.cpabe"
wget -L "${url}PointCloud-dataset/office52-enc-xyz-24fps-noattr/office52-xyz-00001-25.ply.cpabe"
wget -L "${url}PointCloud-dataset/office52-enc-xyz-24fps-noattr/office52-xyz-00001-12.ply.cpabe"
wget -L "${url}PointCloud-dataset/office52-enc-xyz-24fps-noattr/office52-enc-xyz-24fps-noattr.mpd"
wget -L "${url}PointCloud-dataset/office52-enc-xyz-24fps-noattr/cp.sh"
bash cp.sh
rm cp.sh

sudo mv *.ply* /var/www/html/
sudo mv *.mpd* /var/www/html/

