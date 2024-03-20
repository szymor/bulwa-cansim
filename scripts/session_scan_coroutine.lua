-- session scanner for UDS

-- scan settings
diag_req  = 0x18DA0BFA
diag_resp = 0x18DAFA0B
timeout = 1500

function switch_session(sid)
	local msg = { 0x02, 0x10, sid, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }
	msg.id = diag_req
	msg.eff = true
	emit(msg)
	set_timer(timeout)
end

function tester_present()
	local msg = { 0x02, 0x3e, 0x80, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }
	msg.id = diag_req
	msg.eff = true
	emit(msg)
	set_timer(timeout)
end

function wait_for_switch_response()
	local pending = false
	while true do
		local resp = coroutine.yield()
		if resp == nil then
			if pending then
				tester_present()
			else
				-- timeout
				return -2
			end
		elseif resp[2] == 0x7f and resp[3] == 0x10 then
			if resp[4] == 0x78 then
				-- 0x78 - requestCorrectlyReceived-ResponsePending
				tester_present()
				pending = true
			else
				pending = false
				-- could not have switched
				return resp[4]
			end
		elseif resp[2] == 0x50 then
			pending = false
			-- switched successfully
			return -1
		end
	end
end

session_scan = coroutine.wrap(function ()
	for sid = 0, 0xff do
		switch_session(sid)
		local nrc = wait_for_switch_response()
		if nrc >= 0 then
			if nrc == 0x33 or nrc == 0x34 or nrc == 0x7e then
				-- 0x33 - securityAccessDenied
				-- 0x34 - authenticationRequired
				-- 0x7e - SubFunctionNotSupportedInActiveSession
				print(string.format("sid 0x%02x present - NRC 0x%02x\n", sid, nrc))
			elseif nrc ~= 0x12 then
				-- 0x12 - SubFunctionNotSupported
				print(string.format("sid 0x%02x ??????? - NRC 0x%02x\n", sid, nrc))
			end
		elseif nrc == -1 then
			print(string.format("sid 0x%02x present - positive response", sid))
		elseif nrc == -2 then
			print(string.format("sid 0x%02x - timeout", sid))
		end
	end
	disable_node()
end)

function on_enable()
	print("Session scan started")
	session_scan()
end

function on_disable()
	print("Session scan stopped")
end

function on_timer(ms)
	session_scan()
end

function on_message(msg)
	if msg.id == diag_resp then
		session_scan(msg)
	end
end
