-- routine control scanner for UDS

-- scan settings
diag_req  = 0x18DA0BFA
diag_resp = 0x18DAFA0B
timeout = 2000

session_id = 0x01
rid = 0x0000
subfunction = 0x00

function scan_next_rid_or_die()
	if subfunction == 0x02 then
		if rid == 0xffff then
			disable_node()
		else
			rid = rid + 1
			subfunction = 0x01
			routine_control(rid, subfunction)
		end
	elseif subfunction == 0x01 then
		subfunction = 0x03
		routine_control(rid, subfunction)
	elseif subfunction == 0x03 then
		subfunction = 0x02
		routine_control(rid, subfunction)
	elseif subfunction == 0x00 then		-- scan state before sending any rc
		rid = 0x0000
		subfunction = 0x01
		routine_control(rid, subfunction)
	end 
end

function switch_session(sid)
	local msg = { 0x02, 0x10, sid, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }
	msg.id = diag_req
	msg.eff = true
	emit(msg)
	set_timer(timeout)
end

function routine_control(rid, subfunction)
	local msg = { 0x05, 0x31, subfunction, rid >> 8, rid & 0xff, 0x00, 0xcc, 0xcc }
	msg.id = diag_req
	msg.eff = true
	emit(msg)
	set_timer(timeout)
end

function on_enable()
	print("Routine control scan started")
	switch_session(session_id)
end

function on_disable()
	print("Routine control scan stopped")
end

function on_timer(ms)
	if subfunction == 0x00 then
		io.write(string.format("Cannot switch to 0x%02x session\n", session_id))
	else
		io.write(string.format("RID 0x%04x (sf: 0x%02x) - no response\n", rid, subfunction))
	end
	scan_next_rid_or_die()
end

function on_message(msg)
	if msg.id == diag_resp then
		if msg[2] == 0x50 then
			io.write(string.format("Switched to 0x%02x session\n", msg[3]))
			scan_next_rid_or_die()
		elseif msg[2] == 0x71 or (msg[2] == 0x7f and msg[3] == 0x31 and msg[4] ~= 0x31 and msg[4] ~= 0x12 and msg[4] ~= 0x22 and msg[4] ~= 0x7f) then
		-- 0x12 - SubFunctionNotSupported
		-- 0x31 - requestOutOfRange
		-- 0x22 - conditionsNotCorrect
		-- 0x7f - serviceNotSupportedInActiveSession
			if msg[2] == 0x71 then
				io.write(string.format("RID 0x%04x (sf: 0x%02x) present - positive response\n", rid, subfunction))
			else
				io.write(string.format("RID 0x%04x (sf: 0x%02x) present - NRC 0x%02x\n", rid, subfunction, msg[4]))
			end
		end
		if msg[2] ~= 0x50 then
			scan_next_rid_or_die()
		end
	end
end
