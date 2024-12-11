# LEACH-FOR-ESP32
 
# LEACH Protocol Implementation on ESP32 with LoRa

## Overview
This repository contains an Arduino-based implementation of the LEACH (Low Energy Adaptive Clustering Hierarchy) protocol using ESP32 microcontrollers and LoRa E220 modules. LEACH is a popular hierarchical clustering protocol designed primarily for wireless sensor networks to enhance the efficiency of energy consumption.

## Key Features
- **Dynamic Cluster Head Selection**: Nodes autonomously decide whether to become a cluster head based on a calculated probability that considers their simulated battery level.
- **Cluster Member Communication**: Cluster heads broadcast their status, and member nodes respond to confirm their association, facilitating the cluster formation process.
- **Arbitration and Cooldown Mechanisms**: Nodes participate in an arbitration process to elect cluster heads and enter a cooldown period after being a cluster head or upon detecting another cluster head, ensuring balanced energy usage.
- **Retransmission Attempts**: To mitigate message loss due to collisions or other signal issues, member nodes attempt to send their membership multiple times.

## Limitations
- **Collisions**: As nodes may broadcast simultaneously on the same frequency, message collisions are probable.
- **Packet Loss**: Factors like signal interference or LoRa module limitations can lead to packet loss, impacting cluster formation reliability.
- **Energy Efficiency**: Although LEACH aims to be energy-efficient by rotating cluster head roles, this implementation may not optimally conserve energy due to frequent control message exchanges and the overhead of the arbitration process.

## Note
This project is primarily experimental and more of a fun exploration into the implementation of network protocols on microcontrollers rather than a production-ready solution. It's important to note that due to the overhead involved in managing clusters and the arbitration process, this LEACH implementation may consume more energy compared to a direct connection setup. We are not experts in this field, and this project was undertaken as a learning exercise and for entertainment purposes.
