-- (really) dumb CAN fuzzer
-- no oracle used, just spam blindly

fd_on = true

math.randomseed(12345, 67890)

function send_random()
	local len = fd_on and math.random(8,64) or 8
	local msg = { eff = true,
		type = fd_on and "CANFD" or "CAN",
		id = math.random(0x20000000) - 1 }
	for i = 1,len do
		msg[i] = math.random(256) - 1
	end
	emit(msg)
end

function on_enable()
	print("Dumb fuzzing started")
	set_timer(10)
end

function on_disable()
	print("Dumb fuzzing stopped")
end

function on_timer(ms)
	send_random()
	return ms
end

function on_message(msg)
end
