# Pico-10BASE-T
10BASE-T from Raspberry Pi Pico

Note:
* DO NOT CONNECT to PoE equipment!!
* Currently transmission only (UDP is available)
* Receiving process will be implemented in the future...
* It is advisable to insert a pulse transformer for safety.

# Setup
* Raspberry Pi Pico
* 2 x 47 ohm resistor
* 1 x 470 ohm resistor
* RJ45 connector(GP16 = TX-, GP17 = TX+)

Have fun!  
<img src="doc/setup.png" width="500">  

# Ethernet Packet Example
<img src="doc/packet.png" width="500">  

# Ethernet Physical layer waveform
Measured with 100Ω termination.  

NLP(Normal Link Pulse)  
<img src="doc/nlp.png" width="500">  

Ethernet Packet overview  
<img src="doc/packet_2.png" width="500">  

Preamble  
<img src="doc/preamble.png" width="500">  

TP_IDL  
<img src="doc/tp_idl.png" width="500">  

# How to make a pulse transformer
A simple pulse transformer can be built using ferrite cores that have fallen around!  
Adding a transformer ensures insulation and safe experimentation.  

<img src="doc/pt1.png" width="500">  

Pass it through the core about three times.  
<img src="doc/pt2.png" width="500">  

Just connect it into the RasPico and you're done!  
<img src="doc/pt3.png" width="500">  

The waveform after passing through the transformer.  
<img src="doc/pt4.png" width="500">  

# Waveforms of commercial network devices
Physical layer signal waveforms of commercial network equipment operating at 10BASE-T.  
<img src="doc/commercial_network_devices.png" width="500">  

Measured with 100Ω termination.  
<img src="doc/probe.png" width="500">  

