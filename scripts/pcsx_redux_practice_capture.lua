-- Walk the USA title into PRACTICE MODE and its fight with pad 1, pausing at fixed points so scripts/capture_ps1_menus.py
-- (--lua, --names) can save the original screens.
-- No guest writes or savestates. Game state 3 title, 4 main menu, 9 select.
PCSX.settings.pads[1].Connected = true
PCSX.settings.pads[2].Connected = false
local pad = PCSX.SIO0.slots[1].pads[1]
local ptr = PCSX.getMemPtr()
local function u32(a)
    local p = a - 0x80000000
    return tonumber(ptr[p]) + 256 * tonumber(ptr[p+1]) + 65536 * tonumber(ptr[p+2]) + 16777216 * tonumber(ptr[p+3])
end
local START, UP, RIGHT, DOWN, SQUARE, CROSS = 3, 4, 5, 6, 15, 14
-- {state, age, action} (state -1: the age is the absolute vsync count): 'pause:<name>', 'press:<button>' (held 6 vsyncs). Practice mode: PRACTICE MODE is two UP from ARCADE
-- on the main menu; the select takes a SQUARE for each fighter; then the practice fight.
local plan = {
    {3, 400, 'press:' .. START},
    {4, 90, 'press:' .. UP}, {4, 120, 'press:' .. UP}, {4, 160, 'press:' .. SQUARE},
    {9, 100, 'press:' .. SQUARE}, {9, 200, 'press:' .. SQUARE},
    -- MODE SELECT answers from about vsync 3800: 1P FREESTYLE, the dummy to QUICK ROLL, HIT ANALYSIS on, the fight, the command list
    {-1, 3850, 'press:' .. SQUARE},
    {-1, 3920, 'pause:free'},
    {-1, 3940, 'press:' .. DOWN},
    {-1, 3985, 'press:' .. DOWN},
    {-1, 4030, 'press:' .. RIGHT},
    {-1, 4075, 'press:' .. RIGHT},
    {-1, 4120, 'press:' .. RIGHT},
    {-1, 4165, 'press:' .. RIGHT},
    {-1, 4210, 'press:' .. RIGHT},
    {-1, 4255, 'press:' .. RIGHT},
    {-1, 4300, 'pause:dummy'},
    {-1, 4320, 'press:' .. DOWN},
    {-1, 4365, 'press:' .. DOWN},
    {-1, 4410, 'press:' .. DOWN},
    {-1, 4455, 'press:' .. RIGHT},
    {-1, 4515, 'pause:switch'},
    {-1, 4535, 'press:' .. START},
    {-1, 4685, 'pause:fight'},
    {-1, 4705, 'press:' .. START},
    {-1, 4765, 'press:' .. UP},
    {-1, 4810, 'press:' .. UP},
    {-1, 4855, 'press:' .. UP},
    {-1, 4900, 'press:' .. UP},
    {-1, 4945, 'press:' .. SQUARE},
    {-1, 5095, 'pause:list'},
    {-1, 5115, 'press:' .. DOWN},
    {-1, 5235, 'pause:list2'},
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
    if item and ((item[1] == -1 and frames >= item[2]) or (item[1] == state and age >= item[2])) then
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
