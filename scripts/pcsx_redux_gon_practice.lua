-- Play Gon in PRACTICE MODE on the USA disc and pause around his gas (back + RK, "Gon with the Wind") so scripts/capture_ps1_menus.py
-- (--lua, --names) saves the original's versus-style frames. Template: pcsx_redux_practice_capture.lua. One guest write unlocks Gon
-- in the unlock mask (Rev.1 0x800982D0, USA 0x80097EF0 by the save block's 0x3E0 offset, bit 17); no savestates. Game state 3 title,
-- 4 main menu, 9 select. Gon is the eleventh, undrawn cell at the end of the select's row: one LEFT from the first cell reaches him.
-- T3_GON_MOVE=gas (default: LEFT + CIRCLE) or breath (SQUARE + TRIANGLE, LP + RP); the pause frames are 10 vsyncs apart after it.
PCSX.settings.pads[1].Connected = true
PCSX.settings.pads[2].Connected = false
local pad = PCSX.SIO0.slots[1].pads[1]
local ptr = PCSX.getMemPtr()
local function u32(a)
    local p = a - 0x80000000
    return tonumber(ptr[p]) + 256 * tonumber(ptr[p+1]) + 65536 * tonumber(ptr[p+2]) + 16777216 * tonumber(ptr[p+3])
end
local START, UP, RIGHT, DOWN, LEFT, TRIANGLE, CIRCLE, CROSS, SQUARE = 3, 4, 5, 6, 7, 12, 13, 14, 15
local UNLOCK_MASK = 0x80097EF2   -- byte 2 of the unlock mask: bit 17 is its bit 1
local MOVE = os.getenv('T3_GON_MOVE') or 'gas'
local MOVE_BUTTONS = MOVE == 'breath' and {SQUARE, TRIANGLE} or {LEFT, CIRCLE}
local MOVE_AT = tonumber(os.getenv('T3_GON_AT') or '2800')
-- {state, age, action}: state -1 means the age is the absolute vsync count; 'press:<button>[,<button>]' held 6 vsyncs ('hold:' 24)
local plan = {
    {3, 400, 'press:' .. START},
    {4, 5, 'write:1'},
    {4, 90, 'press:' .. UP}, {4, 120, 'press:' .. UP}, {4, 160, 'press:' .. SQUARE},
    {9, 100, 'press:' .. LEFT}, {9, 160, 'press:' .. SQUARE}, {9, 240, 'press:' .. SQUARE},
    {-1, 2560, 'press:' .. SQUARE}, {-1, 2640, 'press:' .. SQUARE},   -- MODE SELECT (open from about vsync 2480): 1P FREESTYLE, then OK
}
if MOVE == 'look' then   -- no move: pause every 60 vsyncs from MOVE_AT to see what the practice screens do
    for k = 1, 8 do plan[#plan + 1] = {-1, MOVE_AT + 60 * (k - 1), 'pause:b' .. k} end
else
    plan[#plan + 1] = {-1, MOVE_AT, 'hold:' .. MOVE_BUTTONS[1] .. ',' .. MOVE_BUTTONS[2]}
    for k = 1, 8 do plan[#plan + 1] = {-1, MOVE_AT + 20 + 10 * k, 'pause:b' .. k} end
end
local state, age, step, release_at = -1, 0, 1, nil
local frames = 0
T3_GON_CAPTURE = PCSX.Events.createEventListener('GPU::Vsync', function()
    frames = frames + 1
    local now = u32(0x800AE204)
    if now ~= state then
        print(string.format('MENU_STATE vsync=%d state=%d', frames, now))
        state, age = now, 0
    end
    age = age + 1
    if release_at and frames >= release_at[1] then
        for _, b in ipairs(release_at[2]) do pad.clearOverride(b) end
        release_at = nil
    end
    local item = plan[step]
    if item and ((item[1] == -1 and frames >= item[2]) or (item[1] == state and age >= item[2])) then
        local kind, arg = item[3]:match('(%a+):(.+)')
        step = step + 1
        if kind == 'press' or kind == 'hold' then
            local buttons = {}
            for b in arg:gmatch('%d+') do buttons[#buttons + 1] = tonumber(b); pad.setOverride(tonumber(b)) end
            release_at = {frames + (kind == 'hold' and 24 or 6), buttons}
        elseif kind == 'write' then
            ptr[UNLOCK_MASK - 0x80000000] = ptr[UNLOCK_MASK - 0x80000000] | 2
            print(string.format('UNLOCK mask byte now %02x', tonumber(ptr[UNLOCK_MASK - 0x80000000])))
        else
            print('MENU_PAUSE ' .. arg)
            PCSX.pauseEmulator()
        end
    end
end)
print('gon practice capture armed')
