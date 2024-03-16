# Bulwa CAN Simulator

## description

BCS is a minimalistic CAN simulator that implements a virtual bus concept known from, among other tools, Vector CANoe. Each network node can be scripted in Lua. Configuration of the environment can be made via editing a JSON configuration file. The application uses SocketCAN so it should be compatible with a wide range of USB-CAN adapters.

The work is in progress.

## custom LUA API

`node_id` - an integer denoting the index of a node running the script,

`node_name` - a string containing the name of a node running the script,

`enable_node(node_name_string)` - enables the node named *node_name_string*,

`disable_node(node_name_string)` - disables the node named *node_name_string*,

`disable_node()` - disable a node running the script,

`set_timer(interval)` - arms the timer of a node with a given time *interval*; if *interval* == 0, then the timer is disarmed,

`emit(msg)` - sends a message over CAN or CAN FD, the *msg* table describes the message to be sent:
- `msg.type` - "CAN" or "CANFD",
- `msg.id` - message identifier,
- `msg.len` - the length of the message,
- `msg.dlc` - CAN only, data length code to be sent; certain controllers allow the transmission or reception of a DLC greater than eight, but the actual data length is always limited to eight bytes,
- `msg.eff` - boolean, Extended Frame Format flag,
- `msg.rtr` - boolean, Remote Transmission Request flag,
- `msg.err` - boolean, Error Message Frame,
- `msg.brs` - CAN FD only, boolean, Bit Rate Switch,
- `msg.esi` - CAN FD only, boolean, Error State Indicator.

### callbacks
`on_enable`

`on_disable`

`on_message(msg)` - *msg* contains details of the received message, the format is the same as for *emit(msg)*,

`on_timer(interval)` - returns non-zero value for a periodic timer, returns zero to stop a timer, returns nil (i.e. nothing) if a timer was previously set in the callback by *set_timer*.

## credits
Code by *szymor* aka *vamastah*.

## to do

- msg data should start from index 1, not 0 - this is lua convention
- msg data len should be considered at transmit if no explicit len is provided
- add iso-tp frame support

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

### send ISO-TP frames
```
echo "2E F1 90 30 31 32 33 34 35 36 37 38 39" | isotpsend -s 18DA0BFA -d 18DAFA0B vcan0
```

### dump messages from CAN bus
```
candump vcan0
```
