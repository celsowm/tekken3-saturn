-- (hit probe) Walk the USA title and main menu to the Tekken Ball item and pause at fixed ages so scripts/capture_ps1_menus.py
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
local START, DOWN, SQUARE = 3, 6, 15
local UNLOCK = 0x80097F26   -- story step 10 (Tekken Ball); Rev.1 0x80098306
-- Ball hit probe: the same walk as pcsx_redux_ball_fire.lua into the Tekken Ball fight, then one log line per frame of the
-- ball's record (the pointer at USA 0x800ADD74: state +0x00, pos +0x68, owner +0x130, target +0x134, charge +0x140) and both
-- fighters' health (USA 0x800A9228 + 0x188C * n, health +0x3F4). Logged only in game state 8 (the fight); state 10 is a one-frame
-- transition in the fight's start (seen in the fire capture). No guest writes after the unlock. Lines start "HIT ".
local FIGHTER0 = 0x800A9228
local BALL_PTR = 0x800ADD74
local LOG_FRAMES = tonumber(os.getenv('T3_HIT_FRAMES') or '2400')
local FORCE_AT = tonumber(os.getenv('T3_HIT_AT') or '1600')
local FORCE_FRAMES = 120
local FORCE_CHARGE = tonumber(os.getenv('T3_HIT_CHARGE') or '50')
local BODY = tonumber(os.getenv('T3_HIT_BODY') or '1')
local UNGUARD = os.getenv('T3_HIT_UNGUARD') == '1'
local FLAGS = os.getenv('T3_HIT_FLAGS') == '1'   -- the charged touch's flags (ball_sim.py _charged_touch), USA addresses
local CHARGED_HIT = 0x800B688C   -- Rev.1 0x800B6B54 (past the volley file's end, a runtime variable) with the slot's delta -0x4C8
local STANDIN = 0x800AC340   -- the third fighter record (the ball's stand-in attacker): Rev.1 0x800AC808
local forced_at = nil
-- T3_HIT_SHOTS: vsyncs after the forced touch at which to pause as 'touch-N' (comma-separated), for frames of the burn and popup
local SHOTS = {}
for n in (os.getenv('T3_HIT_SHOTS') or ''):gmatch('%d+') do SHOTS[tonumber(n)] = true end
local function write32(a, v) local p = a - 0x80000000; v = v % 0x100000000; for k = 0, 3 do ptr[p + k] = (v >> (8 * k)) & 0xFF end end
local function write8(a, v) ptr[a - 0x80000000] = v % 0x100 end
local function write16(a, v) local p = a - 0x80000000; v = v % 0x10000; ptr[p] = v & 0xFF; ptr[p + 1] = v >> 8 end
local function s16(a) local v = u32(a) & 0xFFFF; if v >= 0x8000 then v = v - 0x10000 end; return v end
local function s32(a) local v = u32(a); if v >= 0x80000000 then v = v - 0x100000000 end; return v end
local function u8(a) return tonumber(ptr[a - 0x80000000]) end
local plan = {
    {3, 300, 'write:1'}, {3, 400, 'press:' .. START},
    {4, 5, 'write:1'},
    {4, 60, 'press:' .. DOWN}, {4, 80, 'press:' .. DOWN}, {4, 100, 'press:' .. DOWN}, {4, 120, 'press:' .. DOWN},
    {4, 150, 'pause:menu-4'}, {4, 160, 'press:' .. SQUARE}, {4, 161, 'go:1'},
}
local GO_PRESS_AT = {400, 800, 1200}
local GO_PRESSES = {}
for _, f in ipairs(GO_PRESS_AT) do GO_PRESSES[f] = true end
local go, go_frames = false, 0
local state, age, step, release_at = -1, 0, 1, nil
local frames = 0
T3_HIT_PROBE = PCSX.Events.createEventListener('GPU::Vsync', function()
    frames = frames + 1
    local now = u32(0x800AE204)
    if now ~= state then
        print(string.format('STATE vsync=%d state=%d', frames, now))
        state, age = now, 0
    end
    age = age + 1
    if go then
        go_frames = go_frames + 1
        if GO_PRESSES[go_frames] then pad.setOverride(SQUARE); release_at = {frames + 6, SQUARE} end
        if go_frames == LOG_FRAMES then print('HIT_PAUSE hit-end'); PCSX.pauseEmulator() end
        local b = u32(BALL_PTR)
        -- the forced charged touch: at FORCE_AT of the fight the ball is set flying from the other fighter's side (owner, charge)
        -- onto the touched fighter's body (BODY: its root x and z, its head less 0x200) with the stand-in charge; the touched fighter
        -- takes the charge. The next frames log both
        if not forced_at and go_frames >= FORCE_AT and now == 8 and b >= 0x80000000 and b < 0x80200000 then
            local bp = b - 0x80000000
            -- T3_HIT_BODY: the fighter whose body the ball is placed on (it touches, so it is the hitter); the other one charged the ball
            local v = FIGHTER0 + 0x188C * BODY
            local owner = FIGHTER0 + 0x188C * (1 - BODY)
            local x, z, y = s32(v + 0xF68), s32(v + 0xF70), s32(v + 0x950) - 0x200
            -- T3_HIT_UNGUARD: the touched fighter's powerTimer (+0x122, 120 frames) is set before this frame's fighter update, which
            -- clears its guard (combat.md); the touch and the next HitApply then see an unguarded fighter, as the Ymir test does
            if UNGUARD then write16(v + 0x122, 120) end
            write32(b + 0x130, owner); write32(b + 0x134, owner)   -- owner and target are fighter addresses in the original
            write16(b + 0x0, 2); write16(b + 0x140, FORCE_CHARGE)
            write32(b + 0x68, x); write32(b + 0x6C, y); write32(b + 0x70, z)
            for i = 0, 2 do write32(b + 0xB8 + 4 * i, s32(b + 0x68 + 4 * i)) end
            write32(b + 0x78, 0); write32(b + 0x7C, 0); write32(b + 0x80, 0)
            -- the stand-in (the third record, Rev.1 0x800AC808, USA 0x800AC340 by the layout's offset): the owner's strike copies the
            -- charger's record into it, so the forced touch does the same; the id before the copy shows whether the address is a fighter
            print(string.format('OWNER x=%d z=%d', s32(owner + 0xF68), s32(owner + 0xF70)))
            -- the hit direction of a ball hit is the stand-in's first attack segment (end - start, set_hit_dir), and atkDirX/Y/Z (+0x3D0)
            print(string.format('OWNER seg start=%d,%d,%d end=%d,%d,%d atkdir=%d,%d,%d',
                s32(owner + 0x1AC), s32(owner + 0x1B0), s32(owner + 0x1B4), s32(owner + 0x1B8), s32(owner + 0x1BC), s32(owner + 0x1C0),
                s32(owner + 0x3D0), s32(owner + 0x3D4), s32(owner + 0x3D8)))
            print(string.format('STANDIN before char=%d', u8(STANDIN + 0x18)))
            for k = 0, 0x188C - 1 do ptr[STANDIN - 0x80000000 + k] = ptr[owner - 0x80000000 + k] end
            -- after the copy the strike (ball_sim.py _attack_hit) gives the stand-in back its own index 2: HitApply keeps a Ball hit's
            -- damage only when the attacker is index 2, so a stand-in left with the charger's index deals none. Then its power timer,
            -- the ball's attack word (level 0xFFFF, the unblockable hit) and +0xDF; and the striker's own flags
            write8(STANDIN + 0x1E, 2); write16(STANDIN + 0x122, 1)
            write16(STANDIN + 0x64, 0xFFFF); write8(STANDIN + 0xDF, 1)
            write8(owner + 0x85, 1); write8(owner + 0x88, 1); write8(owner + 0xD1, 1); write8(owner + 0x23, 2)
            if FLAGS then
                -- _charged_touch: the charged-hit flag, the stand-in's defender flag for the victim's index, and two more stand-in flags
                write8(CHARGED_HIT, 1)
                write8(STANDIN + 0x83 + u8(v + 0x1E), 1); write8(STANDIN + 0x88, 1); write8(STANDIN + 0xD1, 1)
                print(string.format('FLAGS chg=%d def=%d', u8(CHARGED_HIT), u8(STANDIN + 0x83 + u8(v + 0x1E))))
            end
            print(string.format('STANDIN after char=%d (owner %d)', u8(STANDIN + 0x18), u8(owner + 0x18)))
            print(string.format('FORCE f=%d pos=%d,%d,%d charge=%d victim=%08x chars=%d,%d', go_frames, x, y, z, FORCE_CHARGE, v,
                u8(FIGHTER0 + 0x18), u8(FIGHTER0 + 0x188C + 0x18)))
            forced_at = go_frames
        end
        if forced_at and SHOTS[go_frames - forced_at] then
            print('HIT_PAUSE touch-' .. (go_frames - forced_at)); PCSX.pauseEmulator()
        end
        if forced_at and go_frames >= forced_at and go_frames <= forced_at + FORCE_FRAMES then
            local v = FIGHTER0 + 0x188C * BODY   -- the touched fighter, which takes the charge
            print(string.format('VIC f=%d health=%d slot=%d locked=%d air=%d level=%d damage=%d guard=%d busy3=%d busy4=%d x=%d y=%d z=%d power=%d got=%d clean=%d sdmg=%d',
                go_frames - forced_at, s32(v + 0x3F4), s16(v + 0xA0), ptr[v + 0x85 - 0x80000000], ptr[v + 0xDB - 0x80000000],
                u32(v + 0x64) % 65536, s16(v + 0x5E), u32(v + 0x66) % 65536, ptr[v + 0xC3 - 0x80000000], ptr[v + 0xC4 - 0x80000000],
                s32(v + 0xF68), s32(v + 0x950), s32(v + 0xF70), s16(v + 0x122), ptr[v + 0xCE - 0x80000000], ptr[v + 0xD0 - 0x80000000], s16(STANDIN + 0x5E)))
            -- the other fighter (the charger) on the same frame: whichever one takes the hit reaction (slot, health) is the defender
            local c = FIGHTER0 + 0x188C * (1 - BODY)
            print(string.format('CHG f=%d health=%d slot=%d guard=%d busy3=%d x=%d y=%d z=%d', go_frames - forced_at, s32(c + 0x3F4),
                s16(c + 0xA0), u32(c + 0x66) % 65536, ptr[c + 0xC3 - 0x80000000], s32(c + 0xF68), s32(c + 0x950), s32(c + 0xF70)))
            print(string.format('BALLF f=%d state=%d charge=%d owner=%d target=%d pos=%d,%d,%d',
                go_frames - forced_at, s16(b), s16(b + 0x140), s32(b + 0x130), s32(b + 0x134), s32(b + 0x68), s32(b + 0x6C), s32(b + 0x70)))
        end
        if go_frames <= LOG_FRAMES and now == 8 and b >= 0x80000000 and b < 0x80200000 then
            print(string.format('HIT f=%d ball=%08x state=%d charge=%d owner=%d target=%d pos=%d,%d,%d h0=%d h1=%d',
                go_frames, b, s16(b), s16(b + 0x140), s32(b + 0x130), s32(b + 0x134),
                s32(b + 0x68), s32(b + 0x6C), s32(b + 0x70),
                s32(FIGHTER0 + 0x3F4), s32(FIGHTER0 + 0x188C + 0x3F4)))
        end
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
            print('HIT_PAUSE ' .. arg)
            PCSX.pauseEmulator()
        end
    end
end)
print('ball hit probe armed')
