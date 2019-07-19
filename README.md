# ethernetSender

This application can help test or debug various Ethenet interfaces.  It supports UDP (unicast and broadcast) and TCP, and has special support for Wake-On-LAN messages to simplify magic packet generation.

### Usage:

```
$ ethernetSender \<ip address\> \<port\> \<protocol\> \[--ignore-response\] \<payload\>
```

The first three arguments, IP address, port and protocol, and the last argument, payload, are always required.  The supported protocols identifiers are:
- **udp**
- **udp-broadcast** (like **udp** but sets SO_BROADCAST)
- **tcp**
- **wol** (like **udp-broadcast** but automatically generates the full magic packet when the MAC address is supplied as the payload, and sets --ignore-response by default)

By default, the application will wait for a response after sending the message.  When the first response is received, the response is printed to screen and the application exits.  To exit immediately after sending the message (i.e. without waiting for a response), specify the **--ignore-response** argument.

The payload is sent as ASCII characters exactly as entered.  The payload may contain spaces (everything after the protocol or **--ignore-response** arguments is considered to be a single payload string).  To send hexidecimal bytes, prefix each byte with **\x**.  For example, to send 0xDEADBEEF, the payload should be entered as \xDE\xAD\xBE\xEF.

When sending Wake-on-LAN messages, ethernetSender will attempt to find the correct broadcast address, if the specified address is not already a broadcast address.  For example, if an IP address is given as 192.168.0.5, and the local PC has an interface configured as 192.168.0.3/24, the IP address used when broadcasting the message will be 192.168.0.255.

#### Examples:
- Establish a TCP connection to 192.168.0.25 on port 35351, send message "command:action" and wait for a response:

```
   $ ethernetSender 192.168.0.25 35351 tcp command:action
```

- Send a UDP packet to 10.10.51.10 on port 23456 containing the string "Hello World!" and do not wait for a response:

```
   $ ethernetSender 10.10.51.10 23456 udp --ignore-response Hello World!
```

- Broadcast a WOL magic packet to wake up a machine on the 192.168.0.0/24 subnet with MAC address 00:01:02:03:04:05 (note that the payload is not an entire magic packet - the MAC address will automatically be used to generate a proper magic packet):

```
   $ ethernetSender 192.168.0.255 9 wol \x00\x01\x02\x03\x04\x05
```
