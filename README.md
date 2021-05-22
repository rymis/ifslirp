# IFSLIRP
This program allows to create a bridge between a physical network inteface and emulated virtual network.
Originally I created the program to flash OpenWRT to the router, because Network Manager is always trying
to prevent you from manual network configuration. But I hope it could be useful for other things too.

This program uses libslirp to emulate the virtual network.

## How does it work?
Program creates RAW socket and binds it to the specified interface. Interface is turned into promiscuous mode, so we
collect all the packets from the interface and can process them.

In the same time ifslirp creates slirp virtual network, with NAT, DNS, TFTP, and BOOTP support. So all devices
connected to the interface will be able to use virtual network the same way they could use a simple router.

## Usage
Use *-h* command line argument to read a help message. Typical usage is:
```
# ifslirp -i eth0 -b firmware.bin
```

If you want to specify the network you can use *-n 192.168.0.0* or other address. When you finished, just press Ctrl+C.

## Compatibility
Program is using PACKET mode of the RAW socket which is Linux specific. It is possible to make a *BSD/Darwin port using
divert sockets. For Windows it is possible to use WinDivert project. If anyone is interested in this I'm ready to do it.
