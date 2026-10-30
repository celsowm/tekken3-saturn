-- Paul against Law on the USA disc; player 2's pad replays a recorded Saturn input tape.
-- Pad overrides only: nothing in the guest is written. Reads the libc rand state
-- (USA 0x800A39B8) and fighter roots at chosen fight ticks and pauses at the target tick.
-- No guest writes or savestates. Load before pcsx_redux_capture_usa.lua.
-- USA select context/cells are explicit points in the pinned release layout.
PCSX.settings.pads[1].Connected = true
PCSX.settings.pads[2].Connected = true
local scenarios = {
    {name='high-hit', buttons={15}},
    {name='mid-hit', buttons={5,6,15}},
    {name='low-hit', buttons={6,14}},
    {name='high-guard', buttons={15}, guard={5}},
    {name='mid-guard', buttons={5,6,15}, guard={5}},
    {name='low-guard', buttons={6,14}, guard={5,6}},
    {name='mid-repeat-to-ko', buttons={5,6,15}},
}
local pads = {PCSX.SIO0.slots[1].pads[1], PCSX.SIO0.slots[2].pads[1]}
local ptr = PCSX.getMemPtr()
local function u16(a)
    local p = a - 0x80000000
    return tonumber(ptr[p]) + 256 * tonumber(ptr[p+1])
end
local function u32(a) return u16(a) + 65536 * u16(a+2) end
local held = {{}, {}}
local function release(player)
    for _, button in ipairs(held[player]) do pads[player].clearOverride(button) end
    held[player] = {}
end
local function down(player, buttons)
    release(player)
    for _, button in ipairs(buttons) do pads[player].setOverride(button) end
    held[player] = buttons
end
local frames, last_state, age, fight_start, attack_start, scenario_index = 0, -1, 0, nil, nil, 1
local function frame()
    frames = frames + 1
    local state = u32(0x800AE204)
    if state ~= last_state then
        print(string.format('PAUL_UI vsync=%d state=%d mode=%d', frames, state, u32(0x800AFA88)))
        last_state, age = state, 0
        release(1); release(2)
        fight_start, attack_start = nil, nil
        scenario_index = 1
    end
    age = age + 1
    if state == 3 then
        if age % 90 == 30 then down(1, {3}) elseif age % 90 == 36 then release(1) end
    elseif state == 4 then
        if age == 40 then down(1, {6}) -- Down: Arcade -> VS with both pads connected
        elseif age == 46 then release(1)
        elseif age == 80 then down(1, {15})
        elseif age == 86 then release(1) end
    elseif state == 9 and age > 40 then
        if age % 20 == 1 then
            for player = 1, 2 do
                local rec = 0x80118628 + 0x24 + (player-1) * 0x7C
                local phase, cell = u32(rec), u32(rec+4)
                if phase == 4 then down(player, {3}) -- join
                elseif phase == 1 and cell < 22 then
                    local char = u16(0x801296C8 + cell * 12 + 6)
                    if char == (player == 1 and 0 or 1) then down(player, {15}) else down(player, {5}) end
                end
                print(string.format('PAUL_SELECT p=%d phase=%d cell=%d',player,phase,cell))
            end
        elseif age % 20 == 7 then release(1); release(2) end
    elseif state == 10 and age > 40 then
        if age % 20 == 1 then
            for player = 1, 2 do
                local rec = 0x800B8D58 + 0x18 + (player-1) * 0xAC
                local phase, col, row = u32(rec), u32(rec+8), u32(rec+12)
                if phase == 3 and not T3_GRID_PRINTED then
                    T3_GRID_PRINTED = true
                    local keys = {}
                    for r = 0, 2 do for c = 0, 6 do
                        keys[#keys+1] = string.format('(%d,%d)=%d', c, r, u16(0x80022768 + 6*(c+7*r) + 4) % 256)
                    end end
                    print('T3_GRID ' .. table.concat(keys, ' '))
                end
                if phase == 3 then
                    local target_col, target_row = nil, nil
                    for r = 0, 2 do
                        for c = 0, 6 do
                            local key = u16(0x80022768 + 6*(c+7*r) + 4) % 256
                            if key == (T3_TAPE_OPTIONS and T3_TAPE_OPTIONS.p2_key or 4) * (player - 1) then target_col, target_row = c, r end
                        end
                    end
                    if target_col ~= nil then
                        if col < target_col then down(player, {5})
                        elseif col > target_col then down(player, {7})
                        elseif row < target_row then down(player, {6})
                        elseif row > target_row then down(player, {4})
                        else down(player, {15}) end
                    end
                elseif phase == 4 then down(player, {15}) end -- accept default handicap
                print(string.format('PAUL_QUICK p=%d phase=%d col=%d row=%d',player,phase,col,row))
            end
        elseif age % 20 == 7 then release(1); release(2) end
    elseif state == 8 then
        local tick = u32(0x800AFA4C)
        local a, b = u16(0x800A9240), u16(0x800AAACC) -- fighter charId +0x18
        local want_b = (T3_TAPE_OPTIONS and T3_TAPE_OPTIONS.p2_char) or 1
        if a ~= 0 or b ~= want_b then return end
        if not T3_SEEN_FIGHT then
            T3_SEEN_FIGHT = true
            print(string.format('T3_FIGHT_ENTER vsync=%d tick=%d chars=%d,%d rand=0x%08X', frames, tick, a, b, u32(0x800A39B8)))
        end
        if T3_LAST_TICK == tick then
            T3_STALL = (T3_STALL or 0) + 1
            -- The VS screen waits for input; hold Cross on pad 2 (override only) until the fight counter moves.
            if T3_STALL == 120 then
                print('T3_VS_PRESS vsync=' .. frames)
                print(pcall(function() pads[2].setOverride(14) end))
            end
            if T3_STALL >= 120 then T3_LAST_TICK = tick; return end
        else
            if T3_STALL and T3_STALL >= 120 then
                print('T3_VS_RELEASE tick=' .. tick)
                pcall(function() pads[2].clearOverride(14) end)
            end
            T3_STALL = 0
        end
        T3_LAST_TICK = tick
        if u32(0x800A9288) == 0 or u32(0x800AAB14) == 0 then return end
        if T3_BASE == nil then
            T3_BASE = tick
            print(string.format('T3_BASE tick=%d vsync=%d', tick, frames))
            T3_SNAP = {}
            for addr = 0x80090000, 0x800AFFFC, 4 do T3_SNAP[#T3_SNAP+1] = u32(addr) end
        elseif T3_BASE ~= nil and not T3_SCANNED and tick >= T3_BASE + 2 then
            T3_SCANNED = true
            local found = 0
            for i = 1, #T3_SNAP do
                local addr = 0x80090000 + (i-1)*4
                local w1, w2 = T3_SNAP[i], u32(addr)
                if w1 ~= w2 then
                    local x = w1
                    for k = 1, 400 do
                        x = (x * 0x41C64E6D + 0x3039) % 4294967296
                        if x == w2 then print(string.format('T3_RAND_CANDIDATE addr=0x%08X steps=%d w1=0x%08X w2=0x%08X', addr, k, w1, w2)); found = found + 1; break end
                    end
                end
            end
            print('T3_RAND_SCAN candidates=' .. found)
            T3_SNAP = nil
        end
        local offset = (T3_TAPE_OPTIONS and T3_TAPE_OPTIONS.offset) or 0
        local target = (T3_TAPE_OPTIONS and T3_TAPE_OPTIONS.pause_tick) or 3393
        local tape = T3_TAPE
        local want = tick - T3_BASE + offset
        while T3_TAPE_AT < #tape and tape[T3_TAPE_AT + 1][1] <= want do T3_TAPE_AT = T3_TAPE_AT + 1 end
        local ev = tape[T3_TAPE_AT]
        if ev then
            local buttons = {}
            local held = ev[4]
            -- sideFlag (+0xC6, read only) says which pad direction is forward: clear = Right, set = Left.
            local side = ptr[0xAAAB4 + 0xC6]
            local fwd, back = 5, 7
            if side ~= 0 then fwd, back = 7, 5 end
            if bit.band(held,1) ~= 0 then buttons[#buttons+1] = fwd end
            if bit.band(held,2) ~= 0 then buttons[#buttons+1] = back end
            if bit.band(held,4) ~= 0 then buttons[#buttons+1] = 4 end   -- up
            if bit.band(held,8) ~= 0 then buttons[#buttons+1] = 6 end   -- down
            if bit.band(held,32) ~= 0 then buttons[#buttons+1] = 15 end -- LP square
            if bit.band(held,64) ~= 0 then buttons[#buttons+1] = 12 end -- RP triangle
            if bit.band(held,128) ~= 0 then buttons[#buttons+1] = 14 end -- LK cross
            if bit.band(held,256) ~= 0 then buttons[#buttons+1] = 13 end -- RK circle
            down(2, buttons)
        end
        release(1)
        if T3_TAPE_LOG and ((tick - T3_BASE) % 100 == 0 or ((tick - T3_BASE) >= target - 40)) then
            local function s32(addr) local v=u32(addr); if v>=2147483648 then return v-4294967296 end; return v end
            print(string.format('T3_TAPE tick=%d idx=%d rand=0x%08X p0root=%d,%d,%d p1root=%d,%d,%d side=%d,%d', tick - T3_BASE, T3_TAPE_AT,
                u32(0x800A39B8), s32(0x800A9228+0xF68), s32(0x800A9228+0xF6C), s32(0x800A9228+0xF70),
                s32(0x800AAAB4+0xF68), s32(0x800AAAB4+0xF6C), s32(0x800AAAB4+0xF70), ptr[0xA9228+0xC6], ptr[0xAAAB4+0xC6]))
        end
        do
            local rel = tick - T3_BASE
            if rel >= 3518 and rel <= 3540 and T3_REC_LAST ~= rel then
                T3_REC_LAST = rel
                for r = 0, 2 do
                    local words = {}
                    for w = 0, 15 do words[#words+1] = string.format('%08X', u32(0x800A3408 + r*64 + w*4)) end
                    print(string.format('T3_FLIP rel=%d rec=%d %s', rel, r, table.concat(words, ' ')))
                end
            end
        end
        if tick - T3_BASE >= target then
            for f = 3490, 3540 do
                local row = T3_RC[f]
                if row then
                    local parts = {}
                    for ra, n in pairs(row) do parts[#parts+1] = string.format('0x%08X:%d', ra, n) end
                    table.sort(parts)
                    print(string.format('T3_RAND_CALLS frame=%d %s', f, table.concat(parts, ' ')))
                end
            end
            print(string.format('T3_TAPE_PAUSE tick=%d', tick))
            PCSX.pauseEmulator()
        end
    end
    if frames % 300 == 0 then
        print(string.format('T3_SESSION vsync=%d guest=%d state=%d',frames,u32(0x800AFA4C),state))
    end
end
T3_TAPE = assert(loadfile(T3_TAPE_FILE))()
-- Read-only Exec hook on the libc rand (USA 0x8007A318): per fight frame, count the calls by caller.
T3_RC = {}
do
    local regs = PCSX.getRegisters()
    T3_RAND_BP = PCSX.addBreakpoint(0x8007A318, 'Exec', 4, 'T3 rand', function()
        if T3_BASE then
            local f = u32(0x800AFA4C) - T3_BASE
            if f >= 3490 and f <= 3540 then
                T3_RC[f] = T3_RC[f] or {}
                local ra = tonumber(regs.GPR.n.ra)
                T3_RC[f][ra] = (T3_RC[f][ra] or 0) + 1
            end
        end
        return true
    end)
end
T3_TAPE_AT = 0
T3_TAPE_LOG = true
T3_PAUL_SESSION = {listener = PCSX.Events.createEventListener('GPU::Vsync', function() local ok, err = pcall(frame); if not ok then print('T3_LUA_ERROR ' .. tostring(err):sub(1,300) .. ' held1=' .. #held[1] .. ' held2=' .. #held[2] .. ' stall=' .. tostring(T3_STALL) .. ' at=' .. tostring(T3_TAPE_AT)); for i, b in ipairs(held[2]) do print('held2', i, tostring(b)) end end end)}
print('Paul-vs-Law tape session armed')
