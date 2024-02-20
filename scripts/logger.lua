start_time = 0

function on_enable()
	print(string.format("start: %s", os.date()))
	start_time = os.time()
end

function on_disable()
	print(string.format("end: %s", os.date()))
end

function on_message(msg)
	diff = msg.timestamp / 1000000000 - start_time

	proto_string = (msg.mtu == 16) and "  CAN" or "CANFD"
	extended_id = string.format("%08x%s", msg.id, msg.eff and "x" or " ")
	rtr_str = msg.rtr and "rtr" or "   "
	err_str = msg.err and "err" or "   "
	brs_str = msg.brs and "brs" or "   "
	esi_str = msg.esi and "esi" or "   "

	io.write(string.format("%11.6f %s %s %s %s %s %s %2d: ", diff, proto_string, extended_id, brs_str, esi_str, rtr_str, err_str, msg.len))
	for i=0,msg.len-1 do io.write(string.format("%02x ", msg[i])) end
	print()
end
