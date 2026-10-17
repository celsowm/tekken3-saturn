-- Reproducible USA UI navigation and pad-only Paul mirror encounter.
-- No guest writes or savestates. Load before pcsx_redux_capture_usa.lua.
-- USA select context/cells are explicit points in the pinned release layout.
PCSX.settings.pads[1].Connected = true
PCSX.settings.pads[2].Connected = true
T3_CAPTURE_OPTIONS = {output='paul-mirror-ko.psx-combat.jsonl',
                      max_frames=6000, pause_on_limit=true,
                      stop_after_health_frames=120}
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
                    if char == 0 then down(player, {15}) else down(player, {5}) end
                end
                print(string.format('PAUL_SELECT p=%d phase=%d cell=%d',player,phase,cell))
            end
        elseif age % 20 == 7 then release(1); release(2) end
    elseif state == 10 and age > 40 then
        if age % 20 == 1 then
            for player = 1, 2 do
                local rec = 0x800B8D58 + 0x18 + (player-1) * 0xAC
                local phase, col, row = u32(rec), u32(rec+8), u32(rec+12)
                if phase == 3 then
                    local target_col, target_row = nil, nil
                    for r = 0, 2 do
                        for c = 0, 6 do
                            local key = u16(0x80022768 + 6*(c+7*r) + 4) % 256
                            if key == 0 then target_col, target_row = c, r end
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
        if T3_PAUL_SESSION_OPTIONS and T3_PAUL_SESSION_OPTIONS.neutral then
            release(1);release(2);return
        end
        local a, b = u16(0x800A9240), u16(0x800AAACC) -- fighter charId +0x18
        if a ~= 0 or b ~= 0 or u32(0x800AFA88) ~= 1 then return end
        if u32(0x800A9288) == 0 or u32(0x800AAB14) == 0 then return end
        local tick = u32(0x800AFA4C)
        if fight_start == nil then fight_start = tick end
        local t = tick - fight_start
        local scenario = scenarios[scenario_index]
        if ptr[0xA9228+0xDE] ~= 0 or ptr[0xAAAB4+0xDE] ~= 0 then
            release(1); release(2); return
        end
        -- Approach using observed roots. Only controller overrides are written.
        if t >= 240 and attack_start == nil then
            local function s32(addr)
                local v=u32(addr); if v>=2147483648 then return v-4294967296 end; return v
            end
            local dx=s32(0x800A9228)-s32(0x800AAAB4)
            local dz=s32(0x800A9230)-s32(0x800AAABC)
            -- Observed body separation is 817..853 units depending on both
            -- fighters' movement. Stop above that contact boundary.
            if dx*dx+dz*dz > 900*900 then down(1, {5}); release(2)
            else
                attack_start=tick
                down(1, scenario.buttons); down(2, {7})
                print(string.format('PAUL_SCENARIO guest=%d name=%s',tick,scenario.name))
            end
        end
        if attack_start ~= nil then
            local elapsed = tick-attack_start
            if elapsed == 3 and scenario.guard then down(2, scenario.guard)
            elseif elapsed == 6 then release(1)
            elseif elapsed == 30 then release(2)
            elseif elapsed >= 90 then
                release(1); release(2); attack_start=nil
                scenario_index=math.min(scenario_index+1,#scenarios)
            end
        end
    end
    if frames % 300 == 0 then
        print(string.format('PAUL_SESSION vsync=%d guest=%d state=%d',frames,u32(0x800AFA4C),state))
    end
end
-- Keep the listener alive: otherwise Lua GC removes its event subscription.
T3_PAUL_SESSION = {listener = PCSX.Events.createEventListener('GPU::Vsync', frame)}
print('Paul pad-only session armed; both controllers connected')
