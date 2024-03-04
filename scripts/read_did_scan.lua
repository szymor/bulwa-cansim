-- read data by identifier scanner for UDS

-- scan settings
diag_req  = 0x18DA0BFA
diag_resp = 0x18DAFA0B
timeout = 2000

session_id = 0x01
did = 0x0000

deferred_time = 2000
deferred_callback = nil

function switch_session(sid)
	local msg = {}
	msg.id = diag_req
	msg.eff = true
	msg.len = 8

	msg[0] = 0x02
	msg[1] = 0x10
	msg[2] = sid
	for i = 3, msg.len-1 do
		msg[i] = 0xcc
	end

	emit(msg)
	set_timer(timeout)
end

function tester_present()
	local msg = {}
	msg.id = diag_req
	msg.eff = true
	msg.len = 8

	msg[0] = 0x02
	msg[1] = 0x3e
	msg[2] = 0x00
	for i = 3, msg.len-1 do
		msg[i] = 0xcc
	end

	emit(msg)
	set_timer(timeout)
end

-- ISO-TP frame, basically means that the transmission should be continued
-- and no more flow control frames need to be sent for the message.
-- It is defined in ISO 15765-2.
function flow_control()
	local msg = {}
	msg.id = diag_req
	msg.eff = true
	msg.len = 8

	msg[0] = 0x30
	for i = 1, msg.len-1 do
		msg[i] = 0x00
	end

	emit(msg)
	set_timer(timeout)
end

function read_did(did)
	local msg = {}
	msg.id = diag_req
	msg.eff = true
	msg.len = 8

	msg[0] = 0x03
	msg[1] = 0x22
	msg[2] = did >> 8
	msg[3] = did & 0xff
	for i = 4, msg.len-1 do
		msg[i] = 0xcc
	end

	emit(msg)
	set_timer(timeout)
end

function deferred_call(func)
	deferred_callback = func
	set_timer(deferred_time)
end

function scan_next_did_or_die()
    if did < 0xffff then
		did = did + 1
		read_did(did)
	else
		disable_node()
	end
end

function on_enable()
	print("Read DID scan started")
	switch_session(session_id)
end

function on_disable()
	print("Read DID scan stopped")
	set_timer(0)	-- optional call
end

function on_timer(ms)
	if deferred_callback ~= nil then
		deferred_callback()
		deferred_callback = nil
	else
		io.write(string.format("RDBI 0x%04x - no response\n", did))
		scan_next_did_or_die()
	end
end

function on_message(msg)
	if msg.id == diag_resp then
		if msg[0] & 0xf0 == 0x00 then
			if msg[1] == 0x50 then
				io.write(string.format("Switched to 0x%02x session\n", session_id))
				did = 0x0000
				read_did(did)
			elseif msg[1] == 0x62 or (msg[1] == 0x7f and msg[2] == 0x22 and msg[3] ~= 0x12 and msg[3] ~= 0x31 and msg[3] ~= 0x11 and msg[3] ~= 0x7e and msg[3] ~= 0x7f and msg[3] ~= 0x78) then
			-- 0x12 - SubFunctionNotSupported
			-- 0x31 - requestOutOfRange
			-- 0x11 - serviceNotSupported
			-- 0x7f - serviceNotSupportedInActiveSession
			-- 0x7e - SubFunctionNotSupportedInActiveSession
			-- 0x78 - requestCorrectlyReceived-ResponsePending
				if msg[1] == 0x62 then
					io.write(string.format("RDBI 0x%04x present - positive response\n", did))
				else
					io.write(string.format("RDBI 0x%04x present - NRC 0x%02x\n", did, msg[3]))
				end
				scan_next_did_or_die()
			elseif msg[1] == 0x7f and msg[2] == 0x22 and msg[3] == 0x78 then
				tester_present()
			else
				scan_next_did_or_die()
			end
		elseif msg[0] & 0xf0 == 0x10 then
		-- check for ISO-TP First Frame, UDS payload starts from offset 2
			if msg[2] == 0x62 then
				io.write(string.format("RDBI 0x%04x present - positive response\n", did))
				flow_control()
				deferred_call(scan_next_did_or_die)
			end
		end
	end
end
