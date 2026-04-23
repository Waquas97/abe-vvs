#!/bin/bash
#traffic control on server to limit local netwrok interface, this will limit speed to cache, clients,etc everyone.

sudo tc qdisc del dev enp6s0f1 root
sudo tc qdisc add dev enp6s0f1 handle 1: root htb default 11
sudo tc class add dev enp6s0f1 parent 1: classid 1:1 htb rate 2880mbit
sudo tc class add dev enp6s0f1 parent 1:1 classid 1:11 htb rate 1440mbit
sudo tc class add dev enp6s0f1 parent 1:1 classid 1:12 htb rate 1440mbit


sudo tc filter add dev enp6s0f1 parent 1: protocol ip prio 1 u32 match ip src 10.10.2.2 match ip dst 10.10.2.3 flowid 1:12 
