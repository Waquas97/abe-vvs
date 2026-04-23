#!/bin/bash
#traffic control on server to limit local netwrok interface, this will limit speed to cache, clients,etc everyone.
sudo tc qdisc del dev enp6s0f0 root
sudo tc qdisc add dev enp6s0f0 handle 1: root htb default 11
sudo tc class add dev enp6s0f0 parent 1:1 classid 1:11 htb rate 720mbit 


