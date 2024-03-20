-- write data by identifier scanner for UDS

-- scan settings
diag_req  = 0x18DA0BFA
diag_resp = 0x18DAFA0B
timeout = 2000

session_id = 0x01
did = 0x0000

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

function write_did(did)
	local msg = { 0x04, 0x2e, did >> 8, did & 0xff, 0x00, 0xcc, 0xcc, 0xcc }
	msg.id = diag_req
	msg.eff = true
	emit(msg)
	set_timer(timeout)
end

function scan_next_did_or_die()
    if did < 0xffff then
		did = did + 1
		write_did(did)
	else
		disable_node()
	end
end

function on_enable()
	print("Write DID scan started")
	switch_session(session_id)
end

function on_disable()
	print("Write DID scan stopped")
end

function on_timer(ms)
	io.write(string.format("WDBI 0x%04x - no response\n", did))
	scan_next_did_or_die()
end

function on_message(msg)
	if msg.id == diag_resp then
		if msg[2] == 0x50 then
			io.write(string.format("Switched to 0x%02x session\n", session_id))
			did = 0x0000
			write_did(did)
		elseif msg[2] == 0x6e or (msg[2] == 0x7f and msg[3] == 0x2e and msg[4] ~= 0x11 and msg[4] ~= 0x12 and msg[4] ~= 0x31 and msg[4] ~= 0x7e and msg[4] ~= 0x7f and msg[4] ~= 0x78) then
		-- 0x11 - serviceNotSupported
		-- 0x12 - SubFunctionNotSupported
		-- 0x31 - requestOutOfRange
		-- 0x7e - SubFunctionNotSupportedInActiveSession
		-- 0x7f - serviceNotSupportedInActiveSession
		-- 0x78 - requestCorrectlyReceived-ResponsePending
			if msg[2] == 0x6e then
				io.write(string.format("WDBI 0x%04x present - positive response\n", did))
			else
				io.write(string.format("WDBI 0x%04x present - NRC 0x%02x\n", did, msg[4]))
			end
			scan_next_did_or_die()
		elseif msg[2] == 0x7f and msg[4] == 0x78 then
			tester_present()
		else
			scan_next_did_or_die()
		end
	end
end
