# Pico-10BASE-T
10BASE-T from Raspberry Pi Pico

Note:
* Currently only transmission only (UDP is available)
* Receiving process will be implemented in the future...
* It is advisable to insert a pulse transformer for safety.

# Setup
* Raspberry Pi Pico
* 2 x 47 ohm register
* 1 x 470 ohm register
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
