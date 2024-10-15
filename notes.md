## Lecture 1: Introduction
# basic stuff

Access networks: cable-based access (current standard)
- Cable runs through neighborhood, splits to each home
- *frequency division multiplexing*: differenct channels transmitted in different frequency bands
- HFC: *Hybrid fiber coax*
- - asymmetric: up to 40 Mbps - 1.2 Gbps downstream transmission rate, 30-100 Mbps upstream transmission rate
- network of cable, fiber attaches homes to ISP router
- - homes share access network to cable headend

Access networks: digital subscriber line (DSL)
- use existing telephone line to central office DSLAM
- dial up internet
- different frequencies designate different data to send (voice, internet)
- - data over DSL phone line goes to Internet
- - voice over DSL phone line goes to telephone net

Wireless
- APs: 802.11b/g/n: 11/54/450 Mbps

Packet transmission delay = L / R
L = packet bits
R - transmission rate (bits/sec)

Links
- Coaxial cable
- - two concentric copper conductors, bidirectional, broadband (100 Mbps/channel)

- Fiber optic cable
- - glass fiber carrying light pulses, each pulse = 1 bit
- - high speed operation: p2p transmission (10-100 Gbps)
- - *low error rate: repeaters spaced far apart, immune to electromagnetic noise*

## Lecture 2: Introduction (cont.)
# Network edge, core, performance

Wireless radio
- bands
- *half-duplex*
- radio link types
- - WiFi
- - wide-area (4G/5G)
- - Bluetooth
- - terrestrial microwave/satellite

Network core (routers/network of routers)

switching local, routing global

store-and-forward packet-switching 
- need to wait for entire packet before transmitting it

queueing packet-switching
- packets wait for transmission over output link
- packets dropped if memory buffer in router fills up

packet-switching vs circuit-switching
- packet-switching when there are many routes and possible senders/receivers
- circuit swithcing for dedicated reserved links 

circuit-switching: FDM & TDM
- Frequency Division Multiplexing (FDM) -> HORIZONTAL
- - optical, electromagnetic frequencies divided into narrow frequency bands
- - each bands dedicated to its own conversation & can transmit @ max rate of the narrow band
- Time Division Multiplexing (TDM) -> VERTICAL
- - time divided into slots 
- - round-robin, each conversation gets max bandwidth

packet delay: 4 sources
- transmission
- nodal processing
- queueing
- propagation
total delay = sum of all variables


## Lecture 3: Introduction (cont.)
# Performance (cont.), security, etc.

