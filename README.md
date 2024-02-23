# Bulwa CAN Simulator

## description

BCS is a minimalistic CAN simulator that implements a virtual bus concept known from, among other tools, Vector CANoe. Each network node can be scripted in Lua. Configuration of the environment can be made via editing a JSON configuration file. The application uses SocketCAN so it should be compatible with a wide range of USB-CAN adapters.

The work is in progress.

## custom LUA API

`node_id` - an integer denoting the index of a node running the script

`node_name` - a string containing the name of a node running the script

`enable_node(<node_name_string>)` - enables the node named *node_name_string*

`disable_node(<node_name_string>)` - disables the node named *node_name_string*

`set_timer(interval)` - arms the timer of a node with a given time *interval*; if *interval* == 0, then the timer is disarmed

### callbacks
`on_enable`

`on_disable`

`on_message(msg)` - *msg* contains details of the received message

`on_timer(interval)` - returns non-zero value for a periodic timer

## credits
Code by *szymor* aka *vamastah*.

## development cheatsheet

### enable virtual CAN module and configure VCAN device
```
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
```

### configure hardware CAN device
```
# CAN
sudo ip link set can0 type can bitrate 500000
# CAN FD
sudo ip link set can0 type can bitrate 500000 dbitrate 5000000 fd on

sudo ip link set up can0
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
