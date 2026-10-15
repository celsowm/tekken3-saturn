-- Tekken 3 USA (SLUS_004.02) live PS1 combat oracle recorder.
-- PCSX-Redux LuaJIT, read-only. NO ROM bytes, patches or savestates.
-- Load with: assert(loadfile("scripts/pcsx_redux_capture_usa.lua"))()
-- To stop early from Lua console: T3_REDUX_CAPTURE.stop("manual stop")
--
-- Address provenance: n0madic/Tekken3Remake:
--   docs/research/code/fighter.md (JP Rev.1 0x188C-byte struct/offsets)
--   docs/research/code/memory-map.md (JP symbolic addresses)
--   tools/remake_import/layouts/usa.json (explicit JP -> USA points).
-- Important: the EXE fingerprint is not checked by this Lua script.
-- Verify the mounted USA SLUS_004.02 before treating these as oracle data.
local CONFIG = {
    output = "tekken3-usa-ps1-combat.jsonl",
    max_frames = 1200,
    -- Guest MIPS addresses for unmodified USA SLUS_004.02 (NOT Japan).
    fighter0 = 0x800A9228,
    fighter_size = 0x188C,
    frame_counter = 0x800AFA4C,
    game_state = 0x800AE204,
    required_game_state = 8, -- FightMain, not its preparation state 7
}

assert(PCSX and PCSX.Events and PCSX.getMemPtr and Support.File,
       "Run this file in PCSX-Redux's Lua editor/console")
assert(CONFIG.max_frames > 0 and CONFIG.max_frames <= 60000,
       "max_frames must be in 1..60000")
if T3_REDUX_CAPTURE and T3_REDUX_CAPTURE.stop then
    T3_REDUX_CAPTURE.stop("new recorder loaded")
end

local ptr = PCSX.getMemPtr()
local RAM_BYTES = 2 * 1024 * 1024
local function offset(address, bytes)
    assert(address >= 0x80000000 and address < 0x80200000,
           "address outside first 2 MiB of KSEG0")
    local v = address - 0x80000000
    assert(v + bytes <= RAM_BYTES, "read crosses RAM boundary")
    return v
end
local function u8(addr)
    return tonumber(ptr[offset(addr, 1)])
end
local function u16(addr)
    local p = offset(addr, 2)
    return tonumber(ptr[p]) + tonumber(ptr[p+1])*256
end
local function s16(addr)
    local v = u16(addr)
    if v >= 32768 then return v - 65536 end
    return v
end
local function u32(addr)
    local p = offset(addr, 4)
    return tonumber(ptr[p]) + tonumber(ptr[p+1])*256 +
           tonumber(ptr[p+2])*65536 + tonumber(ptr[p+3])*16777216
end
local function s32(addr)
    local v = u32(addr)
    if v >= 2147483648 then return v - 4294967296 end
    return v
end
local function as_int(v)
    -- All numbers read here are exact integers within IEEE754's 53 bits.
    return string.format("%.0f", v)
end
local function fighter(base, fighter_id, tick, source_frame)
    local health = s32(base + 0x3F4)
    local max_health = s32(base + 0x3F8)
    local move_slot = s16(base + 0x1A0)
    local pose_frame = s16(base + 0x58)
    local ko_flag = u8(base + 0xDE)
    if max_health <= 0 or health < 0 or health > max_health or
       move_slot < 0 or move_slot >= 4023 or
       pose_frame < 0 or pose_frame > 65535 or ko_flag > 1 then
        return nil, "invalid fighter state/slot/pose/HP (wrong game revision or unfinished fight initialization)"
    end
    -- NOTE: 'phase' / 'frames_left' / 'combo_hits_received' are NOT
    -- inferred from raw PS1 fields; their semantics must be proven first.
    -- The 'psx' object is diagnostic; the existing strict comparator
    -- evaluates top-level frame/slot/health/ko only.
    local raw = {
        state = u32(base+0x60), move_frame = s16(base+0x5C),
        hit_freeze = s16(base+0x76),
        recover_frames = s16(base+0x11A),
        got_hit = u8(base+0xCE), guarded = u8(base+0xCF),
        hit_clean = u8(base+0xD0), counter_hit = u8(base+0xD3),
        in_air = u8(base+0xDB),
        root_x = s32(base+0x00), root_y = s32(base+0x04),
        root_z = s32(base+0x08), facing = u16(base+0x0E)
    }
    local json = string.format(
        '{"tick":%s,"fighter":%d,"move_slot":%d,"pose_frame":%d,' ..
        '"health_q16":%s,"ko":%d,"source_frame":%s,' ..
        '"psx":{"state":%s,"move_frame":%s,"hit_freeze":%s,' ..
        '"recover_frames":%s,"got_hit":%d,"guarded":%d,"hit_clean":%d,' ..
        '"counter_hit":%d,"in_air":%d,"root_x":%s,"root_y":%s,' ..
        '"root_z":%s,"facing":%d}}',
        as_int(tick), fighter_id, move_slot, pose_frame, as_int(health),
        ko_flag, as_int(source_frame), as_int(raw.state),
        as_int(raw.move_frame), as_int(raw.hit_freeze),
        as_int(raw.recover_frames), raw.got_hit, raw.guarded,
        raw.hit_clean, raw.counter_hit, raw.in_air, as_int(raw.root_x),
        as_int(raw.root_y), as_int(raw.root_z), raw.facing)
    return json
end

local capture = {
    active = true, started = false, count = 0,
    last_counter = nil, listener = nil, reset_listener = nil,
    load_listener = nil, quitting_listener = nil, output = CONFIG.output
}
local file = nil
function capture.stop(reason)
    if not capture.active then return end
    capture.active = false
    for _, name in ipairs({"listener", "reset_listener",
                           "load_listener", "quitting_listener"}) do
        local listener = capture[name]
        if listener then listener:remove(); capture[name] = nil end
    end
    if file then file:close(); file = nil end
    print("T3 USA PS1 capture stopped: " .. tostring(reason) ..
          ", frames=" .. tostring(capture.count) ..
          ", output=" .. CONFIG.output)
end

local function sample()
    if not capture.active then return end
    if u32(CONFIG.game_state) ~= CONFIG.required_game_state then
        -- End the clip instead of silently joining separate matches.
        if capture.started then capture.stop("left FightMain state") end
        return
    end
    local guest_before = u32(CONFIG.frame_counter)
    if capture.last_counter == guest_before then return end
    local diff = capture.last_counter and
                 (guest_before - capture.last_counter) % 4294967296 or 1
    if capture.started and diff ~= 1 then
        capture.stop("nonconsecutive guest frames: delta=" .. tostring(diff))
        return
    end
    local base0 = CONFIG.fighter0
    local base1 = base0 + CONFIG.fighter_size
    local first, err1 = fighter(base0, 0, capture.count, guest_before)
    local second, err2 = fighter(base1, 1, capture.count, guest_before)
    if not first or not second then
        if capture.started then
            capture.stop(err1 or err2 or "invalid fighter snapshot")
        end
        return -- allow incomplete pre-fight initialization before first frame
    end
    local guest_after = u32(CONFIG.frame_counter)
    if guest_after ~= guest_before then
        capture.stop("guest frame changed during snapshot")
        return
    end
    if not file then
        file = Support.File.open(CONFIG.output, "TRUNCATE")
        if not file or file:failed() then
            capture.stop("unable to create output file")
            return
        end
    end
    local batch = first .. "\n" .. second .. "\n"
    local success, written = pcall(function() return file:write(batch) end)
    if not success or written ~= #batch then
        capture.stop("short/failed output write")
        return
    end
    capture.started = true
    capture.last_counter = guest_before
    capture.count = capture.count + 1
    if capture.count >= CONFIG.max_frames then
        capture.stop("frame limit reached")
    end
end
local function safe_sample()
    local ok, err = pcall(sample)
    if not ok then capture.stop("Lua capture error: " .. tostring(err)) end
end
capture.listener = PCSX.Events.createEventListener("GPU::Vsync", safe_sample)
capture.reset_listener = PCSX.Events.createEventListener(
    "ExecutionFlow::Reset", function() capture.stop("emulator reset") end)
capture.load_listener = PCSX.Events.createEventListener(
    "ExecutionFlow::SaveStateLoaded",
    function() capture.stop("savestate loaded; replay trace separately") end)
capture.quitting_listener = PCSX.Events.createEventListener(
    "Quitting", function() capture.stop("emulator quitting") end)
T3_REDUX_CAPTURE = capture
print("T3 USA PS1 capture armed (FightMain state 8 only), limit " ..
      tostring(CONFIG.max_frames) .. " frames; NO disc fingerprint check")
