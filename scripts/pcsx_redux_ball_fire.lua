-- (fire reference) Walk the USA title and main menu to the Tekken Ball item and pause at fixed ages so scripts/capture_ps1_menus.py
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
local GO_EVERY, GO_PRESSES = 1000000, {}
for _, f in ipairs(GO_PRESS_AT or {}) do GO_PRESSES[f] = true end
local go, go_frames = false, 0
local state, age, step, release_at = -1, 0, 1, nil
local frames = 0

-- Fire reference: in the fight (state 8) the original's EffectBurn(player) (USA 0x8006EF60, Rev.1 0x8006F248) is called once from
-- the entry of EffectsUpdate (USA 0x8006F5C0): the registers are saved, the burn runs, and a second hit of the same breakpoint
-- (its return address) restores them. Then it pauses every PAUSE_EVERY frames (names b1, b2, ...) for the PS1 frames of the burn.
-- T3_FIRE_MODE=cloud calls the breath cloud spawner (type 10, USA 0x8006ECA4) and =gas the gas cloud spawner (type 20, USA 0x8006EDFC)
-- with fighter 0 (USA 0x800A9228) every 4 frames for 12 calls; the default is the burn.
local MODE = os.getenv('T3_FIRE_MODE') or 'burn'
-- the fighter the spawners run on: T3_FIGHTER 0 (default) or 1 (Rev.1 0x800A96F0 and 0x800AAF7C less 0x4C8: 0x188C apart; in the Ball fight
-- fighter 1 is Gon)
local FIGHTER0 = 0x800A9228 + 0x188C * tonumber(os.getenv('T3_FIGHTER') or '0')
local BURN_FN, UPDATE, BURN_AT, BURN_PLAYER, PAUSE_EVERY = 0x8006EF60, 0x8006F5C0, 700, 0, 5
if MODE == 'cloud' then BURN_FN, BURN_PLAYER, PAUSE_EVERY = 0x8006ECA4, FIGHTER0, 4 end
if MODE == 'gas' then BURN_FN, BURN_PLAYER, PAUSE_EVERY = 0x8006EDFC, FIGHTER0, 4 end
-- T3_FIRE_MODE=breath calls the fire breath spawner (type 6, Rev.1 FUN_8006EC84, USA 0x8006E99C) with fighter 0 the same way
if MODE == 'breath' then BURN_FN, BURN_PLAYER, PAUSE_EVERY = 0x8006E99C, FIGHTER0, 4 end
-- T3_FIRE_MODE=glow T3_GLOW_KIND=6|7|9 allocates a pool object through EffectAlloc (USA 0x8006F4F4), makes it the ball's type 15 glow
-- (+0x4C 0 or 1, +0x40 the hitter) and holds the ball's render kind (the ball record through the pointer at USA 0x800ADD74 (Rev.1 0x800AE23C), + 0x146) at the kind the glow lives on.
local BALL_PTR, GLOW_KIND = 0x800ADD74, tonumber(os.getenv('T3_GLOW_KIND') or '6')   -- the word the ball accessor (Rev.1 0x800B0C70) returns
local function ball_kind_addr() local b = u32(BALL_PTR); if b < 0x80000000 or b >= 0x80200000 then return nil end; return b + 0x146 - 0x80000000 end
-- T3_FIRE_MODE=glow calls the volley overlay's glow starter (Rev.1 0x800B4720, USA 0x800B4258: player + 2 * big) once, with the
-- once-only flag word (Rev.1 0x800B645C, USA 0x800B5F94) cleared; T3_GLOW_PLAYER 0|1, T3_GLOW_BIG 0|1.
local GLOW_PLAYER, GLOW_BIG, GLOW_FLAGS = tonumber(os.getenv('T3_GLOW_PLAYER') or '0'), tonumber(os.getenv('T3_GLOW_BIG') or '0'), 0x800B5F94
if MODE == 'glow' then BURN_FN, BURN_PLAYER, PAUSE_EVERY = 0x800B4258, GLOW_PLAYER, 4 end
local calls = 0
local burn_state, saved, since = 0, nil, nil
local pauses = 0
local function registers() return PCSX.getRegisters() end
T3_BURN_BP = PCSX.addBreakpoint(UPDATE, 'Exec', 4, 'burn-inject', function()
    local r = registers()
    -- the glow's update deletes it unless the ball's render kind is its own: set it right before the pool runs, every frame
    if MODE == 'glow' and burn_state >= 2 and ball_kind_addr() then ptr[ball_kind_addr()] = GLOW_KIND end
    -- the gas emitter (type 19, FUN_80074AD4) lives while the owner's move descriptor (table + 0x28) is 0x1B and its frame is at least
    -- table + 0x2D: keep the fighter inside Gon's gas move once the spawner has fired
    if MODE == 'gas' and burn_state >= 2 then
        local f, t = FIGHTER0 - 0x80000000, u32(FIGHTER0 + 0x54) - 0x80000000
        local desc = u32(0x80000000 + t + 0x28) - 0x80000000
        ptr[desc] = 0x1b; ptr[f + 0x58] = tonumber(ptr[t + 0x2d]) + 1; ptr[f + 0x59] = 0
    end
    if burn_state == 1 then
        saved = {pc = r.pc}
        for i = 0, 31 do saved[i] = r.GPR.r[i] end
        print(string.format('SPAWN f=%08x +54=%08x +58=%08x pos=%d,%d,%d', FIGHTER0, u32(FIGHTER0 + 0x54), u32(FIGHTER0 + 0x58),
            u32(FIGHTER0 + 0x990), u32(FIGHTER0 + 0x994), u32(FIGHTER0 + 0x998)))
        r.GPR.n.a0 = BURN_PLAYER
        -- the cloud spawner's second argument (stored at +0x4c of the object): 0 the breath cloud (CLUT 0x7DCA), nonzero Doctor B.'s (0x7DC6);
        -- T3_CLOUD_ARG sets it (a stale register ended the cloud at once)
        r.GPR.n.a1 = tonumber(os.getenv('T3_CLOUD_ARG') or '0')
        if MODE == 'glow' then
            r.GPR.n.a1 = GLOW_BIG
            for k = 0, 3 do ptr[GLOW_FLAGS - 0x80000000 + k] = 0 end
        end
        if MODE == 'gas' then
            -- the spawner only fires on the move's trigger frame: make the fighter's frame equal the table entry
            local f = FIGHTER0 - 0x80000000
            local table_ptr = u32(FIGHTER0 + 0x54) - 0x80000000
            ptr[u32(0x80000000 + table_ptr + 0x28) - 0x80000000] = 0x1b; ptr[f + 0x58] = ptr[table_ptr + 0x2d]; ptr[f + 0x59] = 0
        end
        r.GPR.n.ra = UPDATE
        r.pc = BURN_FN
        burn_state = 2
    elseif burn_state == 2 and tonumber(r.GPR.n.ra) ~= UPDATE then
        -- not our return: a normal call of EffectsUpdate while the burn has not started
    elseif burn_state == 2 then
        if MODE == 'glow' then
            local obj = tonumber(r.GPR.n.v0)
            print(string.format('GLOW starter v0=%08x ball=%08x kind=%d', tonumber(r.GPR.n.v0), u32(BALL_PTR), ball_kind_addr() and tonumber(ptr[ball_kind_addr()]) or -1))
        end
        for i = 1, 31 do r.GPR.r[i] = saved[i] end
        r.pc = saved.pc
        calls = calls + 1
        burn_state = 3
        since = since or 0
        print('BURN injected')
    end
end)
T3_BALL_CAPTURE = PCSX.Events.createEventListener('GPU::Vsync', function()
    frames = frames + 1
    local now = u32(0x800AE204)
    if now ~= state then
        print(string.format('STATE vsync=%d state=%d mode=%d', frames, now, u32(0x800AFF50)))
        state, age = now, 0
    end
    age = age + 1
    if MODE == 'glow' and since and ball_kind_addr() then ptr[ball_kind_addr()] = GLOW_KIND end
    if state == 8 and age >= BURN_AT and burn_state == 0 then burn_state = 1 end
    if since then
        if MODE ~= 'burn' and calls < ((MODE == 'glow' or MODE == 'gas') and 1 or 12) and since % 4 == 3 then burn_state = 1 end
        since = since + 1
        if since % PAUSE_EVERY == 0 and pauses < 14 then pauses = pauses + 1; print('BALL_PAUSE b' .. pauses); PCSX.pauseEmulator() end
    end
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
