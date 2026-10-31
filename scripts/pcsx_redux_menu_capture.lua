-- Walk the USA title, main menu and character select with pad 1 and pause at
-- fixed points so scripts/capture_ps1_menus.py can save the original screens.
-- No guest writes or savestates. Game state 3 title, 4 main menu, 9 select.
PCSX.settings.pads[1].Connected = true
PCSX.settings.pads[2].Connected = false
local pad = PCSX.SIO0.slots[1].pads[1]
local ptr = PCSX.getMemPtr()
local function u32(a)
    local p = a - 0x80000000
    return tonumber(ptr[p]) + 256 * tonumber(ptr[p+1]) + 65536 * tonumber(ptr[p+2]) + 16777216 * tonumber(ptr[p+3])
end
local START, RIGHT, DOWN, SQUARE = 3, 5, 6, 15
-- {state, age, action}: 'pause:<name>', 'press:<button>' (held 6 vsyncs)
local plan = {
    {3, 60, 'pause:title-fade'}, {3, 330, 'pause:title'}, {3, 400, 'press:' .. START},
    {4, 60, 'pause:menu'}, {4, 90, 'press:' .. SQUARE},
    {9, 100, 'pause:select-a'}, {9, 110, 'press:' .. RIGHT}, {9, 140, 'pause:select-b'},
    {9, 150, 'press:' .. DOWN}, {9, 190, 'pause:select-c'},
}
local state, age, step, release_at = -1, 0, 1, nil
local frames = 0
T3_MENU_CAPTURE = PCSX.Events.createEventListener('GPU::Vsync', function()
    frames = frames + 1
    local now = u32(0x800AE204)
    if now ~= state then
        print(string.format('MENU_STATE vsync=%d state=%d', frames, now))
        state, age = now, 0
    end
    age = age + 1
    if release_at and age >= release_at[2] and state == release_at[1] then
        pad.clearOverride(release_at[3]); release_at = nil
    end
    local item = plan[step]
    if item and item[1] == state and age >= item[2] then
        local kind, arg = item[3]:match('(%a+):(.+)')
        step = step + 1
        if kind == 'press' then
            pad.setOverride(tonumber(arg)); release_at = {state, age + 6, tonumber(arg)}
        else
            print('MENU_PAUSE ' .. arg)
            PCSX.pauseEmulator()
        end
    end
end)
print('menu capture armed')
