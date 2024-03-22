-----------------------------------------------------------------------------
-- Name:        diagnostic_console.lua
-- Purpose:     Simple diagnostic console for UDS
--              Designed to be used within Bulwa CAN Simulator.

-- Author:      Szymon Morawski
-- Created:     March 22, 2024
-- License:     Public Domain
------------------------------------------------------------------------------

-- TODO: ISO-TP support, transmitting raw CAN frames is really inconvenient

version = 1.0
package.cpath = package.cpath..";./?.dll;./?.so;../lib/?.so;../lib/vc_dll/?.dll;../lib/bcc_dll/?.dll;../lib/mingw_dll/?.dll;"

require("wx")

local frame          = nil   -- the main wxFrame
local panel          = nil   -- the child wxPanel of the wxScrolledWindow to draw on
local diagLog        = nil
local reqIdInput     = nil
local resIdInput     = nil

function hex2bytes(hex)
    local data = hex:gsub("%X", "")
    data = data:gsub("%x%x", function(digits) return string.char(tonumber(digits, 16)) end)
    data = { data:byte(1,#data) }
    return data
end

function main()
    frame = wx.wxFrame(wx.NULL, wx.wxID_ANY, "UDS Diagnostic Console",
                       wx.wxDefaultPosition, wx.wxSize(640, 480),
                       wx.wxCAPTION+wx.wxSYSTEM_MENU+wx.wxCLOSE_BOX+wx.wxMINIMIZE_BOX)

    -- Menubar
    local fileMenu = wx.wxMenu()
    fileMenu:Append(wx.wxID_EXIT, "E&xit\tCtrl+Q", "Quit the program")

    local helpMenu = wx.wxMenu()
    helpMenu:Append(wx.wxID_ABOUT, "&About...", "About UDS Diagnostic Console")

    local menuBar = wx.wxMenuBar()
    menuBar:Append(fileMenu, "&File")
    menuBar:Append(helpMenu, "&Help")
    frame:SetMenuBar(menuBar)

    local statusBar = frame:CreateStatusBar(1)
    frame:SetStatusText("UDS Diagnostic Console")

    panel = wx.wxPanel(frame, wx.wxID_ANY, wx.wxDefaultPosition, wx.wxDefaultSize)

    local dataInput = wx.wxTextCtrl(panel, wx.wxID_ANY, "", wx.wxPoint(0, 0),
        wx.wxSize(360, -1), wx.wxTE_PROCESS_ENTER)

    reqIdInput = wx.wxTextCtrl(panel, wx.wxID_ANY, "request", wx.wxPoint(360, 0),
        wx.wxSize(100, -1), 0)

    resIdInput = wx.wxTextCtrl(panel, wx.wxID_ANY, "response", wx.wxPoint(460, 0),
        wx.wxSize(100, -1), 0)

    local sendButton = wx.wxButton(panel, wx.wxID_ANY, "Send", wx.wxPoint(565, 0),
        wx.wxSize(64, -1))

    diagLog = wx.wxListBox(panel, wx.wxID_ANY, wx.wxPoint(0, 40),
        wx.wxSize(629, 350))

    -- File menu events
    frame:Connect(wx.wxID_EXIT, wx.wxEVT_COMMAND_MENU_SELECTED,
        function (event)
            frame:Close(true)
        end )

    -- Help menu events
    frame:Connect(wx.wxID_ABOUT, wx.wxEVT_COMMAND_MENU_SELECTED,
        function (event)
            wx.wxMessageBox('UDS Diagnostic Console\n\n'..
                "This is a Lua script for Bulwa CAN Simulator\n"..
                "allowing sending and receiving diagnostic\n"..
                "frames over CAN. It was created as the first\n"..
                "wxLua demo for BCS, use it however you like.\n\n"..
                "License: Public Domain.\n\n"..
                wxlua.wxLUA_VERSION_STRING.." built with "..wx.wxVERSION_STRING,
                "About UDS Diagnostic Console",
                wx.wxOK + wx.wxICON_INFORMATION,
                frame )
        end )

    send_message = function (event)
        local data = hex2bytes(dataInput:GetValue())
        local msg = { id = tonumber(reqIdInput:GetValue(), 16) }
        for i = 1,#data do
            msg[i] = data[i]
        end
        emit(msg)
    end

    frame:Connect(dataInput:GetId(), wx.wxEVT_TEXT_ENTER, send_message)
    frame:Connect(sendButton:GetId(), wx.wxEVT_BUTTON, send_message)

    frame:Center()
    frame:Show(true)
end

main()

-- The blocking MainLoop() cannot be run here, but at the same time
-- wxLua does not provide Pending/Dispatch interface, so we simply
-- exit the main loop as soon as it has nothing to do. The main loop
-- in this form needs to be called periodically (from on_timer).
-- Ugly workaround, but it works.
frame:Connect(wx.wxEVT_IDLE,
	function (event)
		wx.wxGetApp():ExitMainLoop()
	end )

frame:Connect(wx.wxEVT_DESTROY,
	function (event)
		disable_node()
	end )

function on_enable()
	print("diagnostic console started")
	set_timer(50)
end

function on_disable()
    -- for some reason at this point stdout is closed
    -- so do not expect any console output
	print("diagnostic console stopped")
end

function on_timer(ms)
	wx.wxGetApp():MainLoop()
	return ms
end

function on_message(msg)
    local diagResId = tonumber(resIdInput:GetValue(), 16)
    if type(diagResId) ~= "number" then
        diagResId = 0
    end

    local diagReqId = tonumber(reqIdInput:GetValue(), 16)
    if type(diagReqId) ~= "number" then
        diagReqId = 0
    end

    if msg.id == diagResId or msg.id == diagReqId then
        local string_id
        if msg.eff then
            string_id = string.format("%08x", msg.id)
        else
            string_id = string.format("%03x", msg.id)
        end

        local data = ""
        for i = 1,#msg do
            data = data .. string.format("%02x ", msg[i])
        end

        diagLog:InsertItems({string.format("%s: %s", string_id, data)}, diagLog:GetCount())
        diagLog:SetSelection(diagLog:GetCount() - 1, diagLog:GetCount() - 1)
    end
end
