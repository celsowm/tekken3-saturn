-- Walk the USA title and main menu to the Tekken Ball item and pause at fixed ages so scripts/capture_ps1_menus.py
-- --names can save the original frames. The one guest write unlocks the menu entry (the save block's byte for story
-- step 10: Rev.1 0x80098306, USA 0x80097F26 by the converter's layout); no savestates. Game state 3 title, 4 main menu, 9 select, 8 fight.
-- Needs PCSX-Redux built with scripts/pcsx_redux_reseek.patch: without it the select.ovl load retries forever.
PCSX.settings.pads[1].Connected = true
PCSX.settings.pads[2].Connected = false
local pad = PCSX.SIO0.slots[1].pads[1]
local ptr = PCSX.getMemPtr()
local function u32(a)
    local p = a - 0x80000000
    return tonumber(ptr[p]) + 256 * tonumber(ptr[p+1]) + 65536 * tonumber(ptr[p+2]) + 16777216 * tonumber(ptr[p+3])
end
GO_PRESS_AT = {400, 800, 1200}
local START, DOWN, SQUARE, CROSS = 3, 6, 15, 14
local UNLOCK = 0x80097F26   -- story step 10 (Tekken Ball); Rev.1 0x80098306
local plan = {
    {3, 300, 'write:1'}, {3, 400, 'press:' .. START},
    {4, 5, 'write:1'},
    {4, 60, 'press:' .. DOWN}, {4, 80, 'press:' .. DOWN}, {4, 100, 'press:' .. DOWN}, {4, 120, 'press:' .. DOWN},
    {4, 150, 'pause:menu-4'}, {4, 160, 'press:' .. SQUARE}, {4, 161, 'go:1'},
}
-- after Tekken Ball is chosen: pause every GO_EVERY frames (names f1, f2, ...) and press SQUARE at these frame counts
local GO_EVERY, GO_PRESSES = 200, {}
for _, f in ipairs(GO_PRESS_AT or {}) do GO_PRESSES[f] = true end
local go, go_frames = false, 0
local state, age, step, release_at = -1, 0, 1, nil
local frames = 0
T3_BALL_CAPTURE = PCSX.Events.createEventListener('GPU::Vsync', function()
    frames = frames + 1
    local now = u32(0x800AE204)
    if now ~= state then
        print(string.format('STATE vsync=%d state=%d mode=%d', frames, now, u32(0x800AFF50)))
        state, age = now, 0
    end
    age = age + 1
    if go then
        go_frames = go_frames + 1
        if GO_PRESSES[go_frames] then pad.setOverride(SQUARE); release_at = {frames + 6, SQUARE} end
        if go_frames % GO_EVERY == 0 then
            print('BALL_PAUSE f' .. math.floor(go_frames / GO_EVERY))
            PCSX.pauseEmulator()
        end
    end
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
        if kind == 'go' then
            go = true
        elseif kind == 'write' then
            ptr[UNLOCK - 0x80000000] = tonumber(arg)
            print(string.format('UNLOCK wrote %d at %08x', tonumber(arg), UNLOCK))
        elseif kind == 'press' then
            pad.setOverride(tonumber(arg)); release_at = {frames + 6, tonumber(arg)}
        else
            print('BALL_PAUSE ' .. arg)
            PCSX.pauseEmulator()
        end
    end
end)
print('ball capture armed')
