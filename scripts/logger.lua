function on_enable()
	print(string.format("I am %s the Logger!", nodename))
end

function on_disable()
	print(string.format("Oh no, I, the great %s, am dying a violent death!", nodename))
end

function on_message(msg)
	print(string.format("%0x eff:%q rtr:%q err:%q brs:%q esi:%q len:%02d dlc:%02d mtu:%02d", msg.id, msg.eff, msg.rtr, msg.err, msg.brs, msg.esi, msg.len, msg.dlc, msg.mtu))
	for i=0,msg.len-1 do io.write(string.format("%02x ", msg[i])) end
	print()
end