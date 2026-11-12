-- Walk the USA title and main menu into Tekken Force (the fifth item) and its character select with pad 1,
-- then pause in the fight at fixed ages so scripts/capture_ps1_menus.py --names can save the original frames.
-- No guest writes or savestates. Game state 3 title, 4 main menu, 9 select, 8 fight.
-- Needs PCSX-Redux built with scripts/pcsx_redux_reseek.patch: without it the select.ovl load retries forever.
PCSX.settings.pads[1].Connected = true
PCSX.settings.pads[2].Connected = false
local pad = PCSX.SIO0.slots[1].pads[1]
local ptr = PCSX.getMemPtr()
local function u32(a)
    local p = a - 0x80000000
    return tonumber(ptr[p]) + 256 * tonumber(ptr[p+1]) + 65536 * tonumber(ptr[p+2]) + 16777216 * tonumber(ptr[p+3])
end
local START, DOWN, SQUARE, CROSS = 3, 6, 15, 14
local plan = {
    {3, 400, 'press:' .. START},
    {4, 60, 'press:' .. DOWN}, {4, 80, 'press:' .. DOWN}, {4, 100, 'press:' .. DOWN},
    -- SQUARE confirms, as in pcsx_redux_menu_capture.lua; CROSS skips the select and the loader then never ends.
    {4, 120, 'press:' .. DOWN}, {4, 150, 'pause:menu-force'}, {4, 160, 'press:' .. SQUARE},
    {9, 100, 'pause:select'}, {9, 120, 'press:' .. SQUARE},
    -- State 1 is the overlay loader, which keeps the last picture; pause in FightMain (8) instead.
    {8, 120, 'pause:after-a'}, {8, 600, 'pause:after-b'}, {8, 1500, 'pause:after-c'},
}
local state, age, step, release_at = -1, 0, 1, nil
local frames = 0
T3_FORCE_CAPTURE = PCSX.Events.createEventListener('GPU::Vsync', function()
    frames = frames + 1
    local now = u32(0x800AE204)
    if now ~= state then
        print(string.format('STATE vsync=%d state=%d mode=%d', frames, now, u32(0x800AFF50)))
        state, age = now, 0
    end
    age = age + 1
    if state == 1 and frames % 60 == 0 then
        -- OverlayLoader (USA 0x80052CC4): sub-state 0x800AE224, queue at 0x8009811C (+0x40 count, +0x41 index,
        -- +0x42 target state), 8-byte requests from +0
        local q = 0x8009811C
        local qi = ptr[q + 0x41 - 0x80000000]
        print(string.format('LOADER vsync=%d sub=%d count=%d index=%d target=%d req=%08x %08x', frames,
            u32(0x800AE224) % 65536, tonumber(ptr[q + 0x40 - 0x80000000]), tonumber(qi),
            tonumber(ptr[q + 0x42 - 0x80000000]), u32(q + qi * 8), u32(q + qi * 8 + 4)))
        -- its CD reader (USA 0x800A0698; Rev.1 0x800A0A40): busy byte +7, the request list from +0x20
        local c = 0x800A0698
        print(string.format('CDQ %08x %08x %08x %08x %08x %08x | %08x %08x %08x %08x %08x %08x %08x', u32(c),
            u32(c + 4), u32(c + 8), u32(c + 12), u32(c + 16), u32(c + 20), u32(c + 32), u32(c + 36), u32(c + 40),
            u32(c + 44), u32(c + 48), u32(c + 52), u32(c + 56)))
    end
    if frames % 300 == 0 then
        local regs = PCSX.getRegisters()
        -- the frame pacer (USA): VSync callback 0x800299E0 arms root counter 2 with target [0x80095430]; its event
        -- handler 0x80029784 counts [0x80095434], draws, and sets the flag [0x8009BC5C] the per-frame wait polls.
        print(string.format('BEAT vsync=%d state=%d age=%d mode=%d pc=%08x ra=%08x target=%d armed=%d handled=%d flag=%d',
            frames, state, age, u32(0x800AFF50), tonumber(regs.pc), tonumber(regs.GPR.n.ra), u32(0x80095430),
            u32(0x80095424), u32(0x80095434), u32(0x8009BC5C)))
    end
    if release_at and frames >= release_at[1] then
        pad.clearOverride(release_at[2]); release_at = nil
    end
    local item = plan[step]
    if item and item[1] == state and age >= item[2] then
        local kind, arg = item[3]:match('(%a+):(.+)')
        step = step + 1
        if kind == 'press' then
            pad.setOverride(tonumber(arg)); release_at = {frames + 6, tonumber(arg)}
        else
            print('FORCE_PAUSE ' .. arg)
            PCSX.pauseEmulator()
        end
    end
end)
print('force capture armed')
