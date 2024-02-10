# Bulwa CAN Simulator

## description

BCS is a minimalistic CAN simulator that implements a virtual bus concept known from, among other tools, Vector CANoe. Each network node can be scripted in Lua. Configuration of the environment can be made via editing a JSON configuration file. The application uses SocketCAN so it should be compatible with a wide range of USB-CAN adapters.

The work is in progress.

## credits
Code by *szymor* aka *vamastah*.

## development cheatsheet

### enable virtual CAN module and configure VCAN device
```
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
```

### generate random messages
```
# CAN
while [ true ]; do cansend vcan0 1FFFFFFF#`head -c 8 /dev/urandom | xxd -p`; sleep 1; done

# CAN FD
while [ true ]; do cansend vcan0 1FFFFFFF##0`head -c 16 /dev/urandom | xxd -p`; sleep 1; done
```

### dump messages from CAN bus
```
candump vcan0
```
