function on_enable()
	print("Virtual ECU is on")
	set_timer(1000)
end

function on_disable()
	print("Virtual ECU is off")
end

function on_timer(interval)
	msg = {}
	msg.mtu = 72
	msg.id = 0x7ff
	msg.len = 8
	for i=0,msg.len-1 do
		msg[i] = math.random(0, 255)
	end
	emit(msg)
	return interval
end

function on_message(msg)

end
