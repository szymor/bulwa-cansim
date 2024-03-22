-- Virtual ECU

--[[
The idea is to have a simulated ECU that responds to diagnostic requests.

OBD-II: to be done

UDS:
- sessions 0x01, 0x02, 0x81 and 0x82
- RDIDs: 0xF186, 0xF190
- WDIDs: 0xF190
- RIDs: 0x1234
]]--

obd_req   = { 0x7df, 0x7e4 }
obd_resp  = 0x7ec

uds_req  = 0x18DA0BFA
uds_resp = 0x18DAFA0B

supported_sessions = { 0x01, 0x02 }
current_session = 0x01

rc1234_start = 0
rc1234_stop  = 0

vin = "MY-H4CK15H-3CU-F0R3V3R-1N-MY-H34RT-BULWA-CANSIM-0123456789"

-- ISO-TP frame, basically means that the transmission should be continued
-- and no more flow control frames need to be sent for the message.
-- It is defined in ISO 15765-2.
function flow_control(id)
	local msg = {0x30, 0, 0, 0, 0, 0, 0, 0}
	msg.id = id
	msg.eff = true
	emit(msg)
end

function raw_send(id, data)
	data.id = id
	emit(data)
end

-- ISO-TP frame transmit over CAN
function isotp_send_template(response_id)
	local payload = {}
	local fc_pending = false
	while true do
		local msg, bg = coroutine.yield()
		if bg == nil then
			-- transmit a new message
			fc_pending = false
			if #msg <= 7 then
				-- Single Frame
				local frame = { #msg }
				table.move(msg, 1, 7, 2, frame)
				raw_send(response_id, frame)
				payload = {}
			else
				-- First Frame
				payload = msg
				local len = #payload
				local frame = { 0x10 | (len >> 8), len & 0xff }
				for i = 3, 8 do
					table.insert(frame, table.remove(payload, 1))
				end
				raw_send(response_id, frame)
				fc_pending = true
			end
		else
			-- continue transmission if needed
			-- msg contains a possible FC frame
			if fc_pending then
				-- process flow control frame, todo
				-- for now dump all data at once
				if msg[1] == 0x30 then
					fc_pending = false
					local index = 1
					while #payload > 0 do
						-- craft Consecutive Frames
						local frame = { 0x20 | index }
						index = (index + 1) & 0x0f
						for i = 2, 8 do
							table.insert(frame, table.remove(payload, 1))
						end
						raw_send(response_id, frame)
					end
				end
			end
		end
	end
end

-- ISO-TP frame reception over CAN
function isotp_recv_template(response_id, callback)
	local payload = {}
	while true do
		local msg = coroutine.yield()
		if #msg > 0 then
			if msg[1] & 0xf0 == 0 then
				-- Single Frame
				payload = {}
				payload.len = msg[1] & 0x0f
				for i = 1, payload.len do
					payload[i] = msg[i + 1]
				end
				callback(payload)
				payload = {}
			elseif msg[1] & 0xf0 == 0x10 then
				-- First Frame
				payload = {}
				payload.len = ((msg[1] & 0x0f) << 8) | msg[2]
				local len = payload.len > 6 and 6 or payload.len
				for i = 1, len do
					payload[i] = msg[i + 2]
				end
				flow_control(response_id)
			elseif msg[1] & 0xf0 == 0x20 then
				-- Consecutive Frame
				if payload.len ~= nil then
					local start_idx = #payload
					local len = payload.len - start_idx
					if len > 7 then
						len = 7
					end
					for i = 1, len do
						payload[start_idx + i] = msg[i + 1]
					end
					if #payload == payload.len then
						callback(payload)
					end
				else
					payload = {}
				end
			end
		end
	end
end

uds_send = coroutine.wrap(isotp_send_template)
uds_send(uds_resp)

--[[
NRC list:
0x10	General reject
0x11	Service not supported
0x12	Subfunction not supported
0x13	Incorrect message length or invalid format
0x14	Response too long
0x21	Busy, repeat request
0x22	Conditions not correct
0x24	Request sequence error
0x25	No response from subnet component
0x26	Failure prevents execution of requested action
0x31	Request out of range
0x33	Security access denied
0x34	Authentication failed
0x35	Invalid key
0x36	Exceeded number of attempts
0x37	Required time delay not expired
0x38	Secure data transmission required
0x39	Secure data transmission not allowed
0x3A	Secure data verification failed
0x50	Certificate validation failed, invalid time period
0x51	Certificate validation failed, invalid signature
0x52	Certificate validation failed, invalid chain of trust
0x53	Certificate validation failed, invalid type
0x54	Certificate validation failed, invalid format
0x55	Certificate validation failed, invalid content
0x56	Certificate validation failed, invalid scope
0x57	Certificate validation failed, invalid certificate
0x58	Ownership verification failed
0x59	Challenge calculation failed
0x5A	Setting access right failed
0x5B	Session key creation/derivation failed
0x5C	Configuration data usage failed
0x5D	Deauthentication failed
0x70	Upload download not accepted
0x71	Transfer data suspended
0x72	General programming failure
0x73	Wrong block sequence number
0x78	Request correctly received, response pending
0x7E	Subfunction not supported in active session
0x7F	Service not supported in active session
0x81	RPM too high
0x82	RPM too low
0x83	Engine is running
0x84	Engine is not running
0x85	Engine run time too low
0x86	Temperature too high
0x87	Temperature too low
0x88	Vehicle speed too high
0x89	Vehicle speed too low
0x8A	Throttle/pedal too high
0x8B	Throttle/pedal too low
0x8C	Transmission range not in neutral
0x8D	Transmission range not in gear
0x8F	Brake switch not closed
0x90	Shifter lever not in park
0x91	Torque converter clutch locked
0x92	Voltage too high
0x93	Voltage too low
0x94	Resource temporary unavailable
]]--
function on_uds_msg(payload)
	if #payload == 0 then
		return
	end

	local service = payload[1]
	if service == 0x10 then
		-- Diagnostic Session Control
		if #payload == 1 then
			raw_send(uds_resp, {0x03, 0x7f, 0x10, 0x13})
		elseif #payload >= 2 then
			local session = payload[2] & 0x7f
			for i = 1, #supported_sessions do
				local sid = supported_sessions[i]
				if sid == session then
					current_session = session
					if payload[2] & 0x80 == 0 then
						raw_send(uds_resp, {0x02, 0x50, session})
					end
				end
			end
			if current_session ~= session then
				raw_send(uds_resp, {0x03, 0x7f, 0x10, 0x12})
			end
		end
	elseif service == 0x22 then
		-- Read Data By Identifier
		if #payload ~= 3 then
			raw_send(uds_resp, {0x03, 0x7f, 0x22, 0x13})
		else
			local did = (payload[2] << 8) | payload[3]
			if did == 0xf186 then
				-- ActiveDiagnosticSession
				raw_send(uds_resp, {0x04, 0x62, 0xf1, 0x86, current_session})
			elseif did == 0xf190 then
				-- VIN
				uds_send({ 0x62, 0xf1, 0x90, vin:byte(1, #vin) })
			else
				raw_send(uds_resp, {0x03, 0x7f, 0x22, 0x31})
			end
		end
	elseif service == 0x2e then
		-- Write Data By Identifier
		if #payload <= 3 then
			raw_send(uds_resp, {0x03, 0x7f, 0x2e, 0x13})
		else
			local did = (payload[2] << 8) | payload[3]
			if did == 0xf190 then
				if current_session == 0x02 then
					vin = string.char(table.unpack(payload))
					vin = vin:sub(4)
					raw_send(uds_resp, {0x03, 0x6e, 0xf1, 0x90})
				else
					raw_send(uds_resp, {0x03, 0x7f, 0x2e, 0x7e})
				end
			else
				raw_send(uds_resp, {0x03, 0x7f, 0x2e, 0x31})
			end
		end
	elseif service == 0x31 then
		-- Routine Control
		if #payload < 4 then
			raw_send(uds_resp, {0x03, 0x7f, 0x31, 0x13})
		else
			local subf = payload[2]
			local rid = (payload[3] << 8) | payload[4]
			if rid == 0x1234 then
				if subf == 0x01 then
					-- startRoutine
					rc1234_start = os.time()
					rc1234_stop = os.time()
				elseif subf == 0x02 then
					-- stopRoutine
					rc1234_stop = os.time()
				elseif subf == 0x03 then
					-- requestRoutineResults
					print(string.format("%s: timediff %7.3f", node_name, rc1234_stop - rc1234_start))
				end
				raw_send(uds_resp, {0x03, 0x71, subf, 0x12, 0x34})
			else
				raw_send(uds_resp, {0x03, 0x7f, 0x31, 0x31})
			end
		end
	elseif service == 0x3e then
		-- Tester Present
		if #payload == 1 then
			raw_send(uds_resp, {0x03, 0x7f, 0x3e, 0x13})
		elseif #payload >= 2 then
			if payload[2] == 0x00 then
				raw_send(uds_resp, {0x02, 0x7e, 0x00})
			elseif payload[2] == 0x80 then
				-- no response
			else
				raw_send(uds_resp, {0x03, 0x7f, 0x3e, 0x12})
			end
		end
	else
		raw_send(uds_resp, {0x03, 0x7f, service, 0x11})
	end
end

uds_recv = coroutine.wrap(isotp_recv_template)
uds_recv(uds_resp, on_uds_msg)

function on_enable()
	print("Virtual ECU is on")
end

function on_disable()
	print("Virtual ECU is off")
end

function on_message(msg)
	if msg.id == uds_req then
		uds_send(msg, true)
		uds_recv(msg)
	end
end
