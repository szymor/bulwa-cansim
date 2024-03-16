-- session scanner for UDS

-- scan settings
diag_req  = 0x18DA0BFA
diag_resp = 0x18DAFA0B
timeout = 2000

session_id = 0x01

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

function scan_next_session_or_die()
	if session_id < 0xff then
		session_id = session_id + 1
		switch_session(session_id)
	else
		disable_node()
	end
end

function on_enable()
	print("Session scan started")
	switch_session(session_id)
end

function on_disable()
	print("Session scan stopped")
end

function on_timer(ms)
	io.write(string.format("sid 0x%02x - no response\n", session_id))
	scan_next_session_or_die()
end

function on_message(msg)
	if msg.id == diag_resp then
		if msg[1] == 0x50 then
			io.write(string.format("sid 0x%02x present - positive response\n", session_id))
			scan_next_session_or_die()
		elseif msg[1] == 0x7f and msg[2] == 0x10 then
			if msg[3] == 0x78 then -- requestCorrectlyReceived-ResponsePending
				tester_present();
			end

			-- 0x33 - securityAccessDenied
			-- 0x34 - authenticationRequired
			-- 0x7e - SubFunctionNotSupportedInActiveSession
			if msg[3] == 0x33 or msg[3] == 0x34 or msg[3] == 0x7e then
				io.write(string.format("sid 0x%02x present - NRC 0x%02x\n", session_id, msg[3]))
				scan_next_session_or_die()
			elseif msg[3] ~= 0x78 then
				if msg[3] ~= 0x12 then	-- SubFunctionNotSupported
					io.write(string.format("sid 0x%02x ??????? - NRC 0x%02x\n", session_id, msg[3]))
				end
				scan_next_session_or_die()
			end
		end
	end
end
