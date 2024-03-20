-- session scanner for UDS

-- scan settings
diag_req  = 0x18DA0BFA
diag_resp = 0x18DAFA0B
timeout = 2000

session_id = 0x01
service_id = 0x00

function switch_session(sid)
	local msg = { 0x02, 0x10, sid, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }
	msg.id = diag_req
	msg.eff = true
	emit(msg)
	set_timer(timeout)
end

function tester_present()
	local msg = { 0x02, 0x3e, 0x00, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }
	msg.id = diag_req
	msg.eff = true
	emit(msg)
	set_timer(timeout)
end

function check_service(sid)
	local msg = { 0x01, sid, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }
	msg.id = diag_req
	msg.eff = true
	emit(msg)
	set_timer(timeout)
end

function scan_next_service_or_die()
	if service_id < 0xff then
		service_id = service_id + 1
		if service_id == 0x3f then
		-- do not check 0x3f as the positive response (0x3f + 0x40)
		-- is the same as the negative response
			service_id = 0x40
		end
		check_service(service_id)
	else
		disable_node()
	end
end

function on_enable()
	io.write(string.format("Service scan for session 0x%02x started\n", session_id))
	switch_session(session_id)
end

function on_disable()
	io.write(string.format("Service scan for session 0x%02x stopped\n", session_id))
end

function on_timer(ms)
	io.write(string.format("Service 0x%02x - no response\n", service_id))
	scan_next_service_or_die()
end

function on_message(msg)
	if msg.id == diag_resp then
		if msg[2] == 0x50 then
			io.write(string.format("Switched to 0x%02x session\n", session_id))
			service_id = 0x00
			check_service(service_id)
		elseif msg[2] == (service_id + 0x40) or (msg[2] == 0x7f and msg[3] == service_id and msg[4] ~= 0x11 and msg[4] ~= 0x7f) then
		-- 0x11 - serviceNotSupported
		-- 0x7f - serviceNotSupportedInActiveSession
			io.write(string.format("Service 0x%02x present - ", service_id))
			if msg[2] == (service_id + 0x40) then
				io.write(string.format("positive response\n"))
			else
				io.write(string.format("NRC 0x%02x\n", msg[4]))
			end
			scan_next_service_or_die()
		elseif msg[2] == 0x7f and msg[4] == 0x78 then
		-- 0x78 - requestCorrectlyReceived-ResponsePending
			tester_present()
		else
			scan_next_service_or_die()
		end
	end
end
