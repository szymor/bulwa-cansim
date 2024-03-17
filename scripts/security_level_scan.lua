-- security level scanner for UDS

-- scan settings
diag_req  = 0x18DA0BFA
diag_resp = 0x18DAFA0B
timeout = 2000

session_id = 0x01
subfunction = 0x01

deferred_time = 2000
deferred_callback = nil

--[[ level value follows certain rules:
	- is always an odd number for requestSeed,
	- is always an even number for sendKey,
	- 0x00 and 0xff are invalid values.
--]]
function security_access(level)
	msg = {}
	msg.id = diag_req
	msg.eff = true
	msg.len = 8

	msg[0] = 0x02
	msg[1] = 0x27
	msg[2] = level
	for i = 3, msg.len-1 do
		msg[i] = 0xcc
	end

	emit(msg)
	set_timer(timeout)
end

function switch_session(sid)
	msg = {}
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
	msg = {}
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
	msg = {}
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

function deferred_call(func)
	deferred_callback = func
	set_timer(deferred_time)
end

function scan_next_level_or_die()
    if subfunction < 0xfd then
		subfunction = subfunction + 2
		security_access(subfunction)
	else
		disable_node()
	end
end

function on_enable()
	print("Security level scan started")
	switch_session(session_id)
end

function on_disable()
	print("Security level scan stopped")
	set_timer(0)	-- optional call
end

function on_timer(ms)
	if deferred_callback ~= nil then
		deferred_callback()
		deferred_callback = nil
	else
		io.write(string.format("security level 0x%02x - no response\n", subfunction))
		scan_next_level_or_die()
	end
end

function on_message(msg)
	if msg.id == diag_resp then
		if msg[0] & 0xf0 == 0x00 and msg[1] == 0x50 then
			io.write(string.format("Switched to 0x%02x session\n", session_id))
			subfunction = 0x01
			security_access(subfunction)
		elseif msg[0] & 0xf0 == 0x00 and (msg[1] == 0x67 or (msg[1] == 0x7f and msg[2] == 0x27 and msg[3] ~= 0x12 and msg[3] ~= 0x31 and msg[3] ~= 0x11 and msg[3] ~= 0x7e and msg[3] ~= 0x7f)) then
		-- a positive response for 0x27 or a negative response with a proper NRC
		-- 0x12 - SubFunctionNotSupported
		-- 0x31 - requestOutOfRange
		-- 0x11 - serviceNotSupported
		-- 0x7e - SubFunctionNotSupportedInActiveSession
		-- 0x7f - serviceNotSupportedInActiveSession
			io.write(string.format("security level 0x%02x present\n", subfunction))
			scan_next_level_or_die()
		elseif msg[0] & 0xf0 == 0x00 and msg[1] == 0x7f and msg[3] == 0x78 then
		-- 0x78 - requestCorrectlyReceived-ResponsePending
			tester_present()
		elseif msg[0] & 0xf0 == 0x10 and msg[2] == 0x67 then
		-- check for ISO-TP First Frame, UDS payload starts from offset 2
			io.write(string.format("security level 0x%02x present\n", subfunction))
			flow_control()
			-- wait for all traffic to come, then scan the next security level
			deferred_call(scan_next_level_or_die)
		elseif msg[0] & 0xf0 == 0x00 then
			-- if any single frame response comes, scan the next security level
			scan_next_level_or_die()
		end
	end
end
