-- Read-only original USA frame-phase observer. Requires -debugger.
-- Configure addresses only via prepare_psx_phase_capture.py (EXE/layout hashes).
-- Retained Exec hooks observe function entry and the caller's return PC.
-- GPU::Vsync samples have a separate sequence; no tick alignment is assumed.
local cfg = assert(T3_PHASE_OPTIONS, 'prepare the fingerprinted launcher first')
local ffi = require('ffi')
local ptr, regs = PCSX.getMemPtr(), PCSX.getRegisters()
local function unsigned(a, n)
    assert(a >= 0x80000000 and a+n <= 0x80200000, 'RAM address out of bounds')
    local v = 0
    for i=n-1,0,-1 do v=v*256+tonumber(ptr[a-0x80000000+i]) end
    return v
end
local function signed(a, n)
    local v=unsigned(a,n)
    if v >= 2^(n*8-1) then v=v-2^(n*8) end
    return v
end
local fields = {
    {'slot',0xA0,2,true}, {'pose',0x58,2,true}, {'move_frame',0x5C,2,true},
    {'health',0x3F4,4,true}, {'state',0x60,4}, {'damage',0x5E,2,true},
    {'attack',0x64,2}, {'guard',0x66,2}, {'freeze',0x76,2,true},
    {'got_hit',0xCE,1}, {'guarded',0xCF,1}, {'hit_clean',0xD0,1},
    {'hit_done0',0x83,1}, {'hit_done1',0x84,1}, {'cooldown',0x104,2,true},
    {'direction',0x402,2}, {'pressed',0x404,2}, {'held',0x406,2},
    {'pending_slot',0x1A0,2,true}, {'root_x',0,4,true}, {'root_z',8,4,true},
    {'root_frame',0x50,2,true}, {'frame_step',0x6E,2,true},
    {'hold_frames',0x6A,2,true}, {'ballistic',0x92,2,true},
    {'move_changed',0xDA,1}, {'active',0xC3,1}, {'pending_move',0x1A4,4},
    {'entry_frame',0x198,2,true}, {'branch_window',0x6C,2,true},
    {'branch_kind',0x8B,1}, {'transition',0x19A,2,true},
    {'current_transition',0xA2,2,true}, {'hit_freeze_in',0xEE,2,true},
    {'pose_move',0x54,4}, {'root_move',0x4C,4},
}
local obs={active=true, hooks={}, returns={}, pending={}, seq=0, calls=0}
T3_PHASE_CAPTURE=obs
local file=nil
local function le32(value)
    return string.char(value%256,math.floor(value/256)%256,math.floor(value/65536)%256,math.floor(value/16777216)%256)
end
local function ground_ram(invocation,edge,prefix)
    local path=(prefix or cfg.ground_prefix)..invocation.id..'-'..edge..'.ranges'
    local ranges={{0x800A9228,2*0x188C},{cfg.step_x,8},{cfg.step_z,8},
                  {0x800AFA4C,4},{0x800AE204,4},{0x800AFA88,4}}
    for _,r in ipairs(cfg.extra_ranges or {}) do ranges[#ranges+1]=r end
    local seen={}
    for player=0,1 do
        local base=0x800A9228+player*0x188C
        for _,offset in ipairs({0x54,0x4C,0x1A4}) do
            local row=unsigned(base+offset,4)
            if row~=0 and not seen[row] then
                ranges[#ranges+1]={row,56};seen[row]=true
                if unsigned(row+0x2D,1)~=0 then
                    local descriptor=unsigned(row+0x28,4)
                    if descriptor~=0 and not seen[descriptor] then
                        ranges[#ranges+1]={descriptor,cfg.contact_prefix and (70+20*unsigned(row+0x18,1)) or 4};seen[descriptor]=true
                    end
                end
            end
        end
        local reaction=unsigned(base+0x1A8,4)
        if reaction~=0 and not seen[reaction] then
            ranges[#ranges+1]={reaction,42};seen[reaction]=true
        end
    end
    local snapshot=Support.File.open(path,'TRUNCATE')
    assert(snapshot and not snapshot:failed(),'unable to open ground ranges')
    local header='T3RANGES'..le32(#ranges)
    assert(snapshot:write(header)==#header,'short range header')
    for _,r in ipairs(ranges) do
        assert(r[1]>=0x80000000 and r[1]+r[2]<=0x80200000,'ground range outside RAM')
        local bytes=le32(r[1])..le32(r[2])..ffi.string(ptr+(r[1]-0x80000000),r[2])
        assert(snapshot:write(bytes)==#bytes,'short ground range write')
    end
    snapshot:close()
end
local function hit_ram(invocation, edge)
    local path=cfg.ram_prefix..invocation.id..'-'..edge..'.bin'
    local snapshot=Support.File.open(path,'TRUNCATE')
    assert(snapshot and not snapshot:failed(),'unable to open hit RAM output')
    local data=ffi.string(ptr,2*1024*1024)
    assert(snapshot:write(data)==#data,'short hit RAM write')
    snapshot:close()
end
local function emit(phase, call, pc, a0)
    local frame=unsigned(0x800AFA4C,4)
    if not obs.active or frame<cfg.first or frame>cfg.last then return end
    if unsigned(0x800AE204,4)~=8 then return end
    if unsigned(0x800A9228+0x60,4)==0 then return end
    if not file then
        file=Support.File.open(cfg.output,'TRUNCATE')
        assert(file and not file:failed(),'unable to open phase output')
    end
    local pair={}
    for player=0,1 do
        local base=0x800A9228+0x188C*player
        local values={}
        for _, f in ipairs(fields) do
            local v=f[4] and signed(base+f[2],f[3]) or unsigned(base+f[2],f[3])
            values[#values+1]=string.format('"%s":%.0f',f[1],v)
        end
        local pose_move=unsigned(base+0x54,4)
        local hold_frame=unsigned(pose_move+0x1B,1)
        values[#values+1]=string.format('"pose_hold_frame":%d,"root_is_pose":%d',
            hold_frame,unsigned(base+0x4C,4)==pose_move and 1 or 0)
        local root_move=unsigned(base+0x4C,4)
        local pending_move=unsigned(base+0x1A4,4)
        values[#values+1]=string.format('"pose_length":%d,"root_length":%d,"pose_animation":%.0f',
            unsigned(pose_move+0x18,1),unsigned(root_move+0x18,1),unsigned(pose_move,4))
        local pending_values={}
        for _, field in ipairs({{'pending_length',0x18,1},{'pending_first',0x2D,1},
                               {'pending_last',0x2E,1},{'pending_animation',0,4}}) do
            pending_values[#pending_values+1]=string.format('"%s":%.0f',field[1],
                pending_move~=0 and unsigned(pending_move+field[2],field[3]) or 0)
        end
        values[#values+1]=table.concat(pending_values,',')
        pair[#pair+1]='{'..table.concat(values,',')..'}'
    end
    local row=string.format(
        '{"seq":%d,"source_frame":%.0f,"cycles":%.0f,"phase":"%s",'..
        '"call":%d,"pc":%.0f,"a0":%.0f,"fighters":[%s]}\n',
        obs.seq,frame,tonumber(PCSX.getCPUCycles()),phase,call,pc,a0,
        table.concat(pair,','))
    assert(file:write(row)==#row,'short phase capture write')
    obs.latest=row
    obs.seq=obs.seq+1
end
function obs.stop(reason)
    if not obs.active then return end
    obs.active=false
    -- Disable retained callbacks rather than freeing the currently executing
    -- FFI callback; the emulator owns their lifetime until script/process exit.
    for _, bp in pairs(obs.hooks) do bp:disable() end
    for _, bp in pairs(obs.returns) do bp:disable() end
    if obs.vsync then obs.vsync:remove() end
    if file then file:close(); file=nil end
    print('T3 phase capture stopped: '..reason..', observations='..obs.seq)
end
for name, address in pairs(cfg.functions) do
    obs.hooks[name]=PCSX.addBreakpoint(address,'Exec',4,'T3 '..name,function()
        if not obs.active then return true end
        if unsigned(0x800AE204,4)~=8 then return true end
        local frame=unsigned(0x800AFA4C,4)
        if frame<cfg.first or frame>cfg.last then return true end
        assert(not obs.pending[name], 'unexpected recursive phase '..name)
        obs.calls=obs.calls+1
        local invocation={id=obs.calls,ra=tonumber(regs.GPR.n.ra),
                          sp=tonumber(regs.GPR.n.sp),a0=tonumber(regs.GPR.n.a0)}
        obs.pending[name]=invocation
        emit(name..':entry',invocation.id,address,invocation.a0)
        if cfg.frame_prefix and name=='FightFrame' then
            if not obs.frame_seeded then
                local ready=true
                for player=0,1 do
                    local b=0x800A9228+player*0x188C
                    ready=ready and unsigned(b+0xA0,2)==3 and unsigned(b+0x1A4,4)==0
                        and unsigned(b+0x4C,4)==unsigned(b+0x54,4)
                        and signed(b+0x58,2)>=1 and signed(b+0x50,2)>=1
                        and unsigned(b+0x406,2)==0 and unsigned(b+0x404,2)==0
                        and unsigned(b+0x3F4,4)==unsigned(b+0x3F8,4)
                    for k=0,59 do ready=ready and unsigned(b+0x41C+k,1)<16 end
                end
                if ready then
                    local f=Support.File.open(cfg.frame_seed,'TRUNCATE')
                    assert(f and not f:failed(),'unable to write frame seed')
                    local data=ffi.string(ptr,2*1024*1024)
                    assert(f:write(data)==#data,'short frame seed write');f:close()
                    obs.frame_seeded=true;obs.frame_seed_call=invocation.id
                    print('T3 frame replay seed call='..invocation.id..' source='..frame)
                end
            end
            if obs.frame_seeded then invocation.frame_ram=true end
        end
        if cfg.ram_prefix and name=='HitApply' and
           (invocation.a0==0x800A9228 or invocation.a0==0x800AAAB4) and
           unsigned(invocation.a0+0xCE,1)~=0 then
            invocation.hit_ram=true
            hit_ram(invocation,'before')
        end
        if cfg.ground_prefix and name=='FighterMovePhysics' then
            invocation.ground_ram=cfg.ground_prefix;ground_ram(invocation,'before')
        end
        if cfg.start_prefix and name=='MoveStartOrAdvance' and
           unsigned(invocation.a0+0x1A4,4)~=0 then
            invocation.ground_ram=cfg.start_prefix;ground_ram(invocation,'before',cfg.start_prefix)
        end
        if (name=='BodySeparate' and cfg.body_prefix) or (name=='ArenaBounds' and cfg.arena_prefix) then
            invocation.ground_ram=name=='BodySeparate' and cfg.body_prefix or cfg.arena_prefix
            ground_ram(invocation,'before',invocation.ground_ram)
        end
        if (name=='HitTest' and cfg.contact_prefix) or (name=='CollisionShapesUpdate' and cfg.shape_prefix) then
            invocation.ground_ram=name=='HitTest' and cfg.contact_prefix or cfg.shape_prefix
            ground_ram(invocation,'before',invocation.ground_ram)
        end
        local key=name..':'..invocation.ra
        if not obs.returns[key] then
            obs.returns[key]=PCSX.addBreakpoint(invocation.ra,'Exec',4,
                'T3 return '..name,function()
                local pending=obs.pending[name]
                if pending and pending.ra==tonumber(regs.pc) and
                   pending.sp==tonumber(regs.GPR.n.sp) then
                    emit(name..':exit',pending.id,pending.ra,pending.a0)
                    if pending.frame_ram then ground_ram(pending,'after',cfg.frame_prefix) end
                    if pending.hit_ram then hit_ram(pending,'after') end
                    if pending.ground_ram then ground_ram(pending,'after',pending.ground_ram) end
                    obs.pending[name]=nil
                    if name=='FightFrame' and unsigned(0x800AFA4C,4)>=cfg.last then
                        obs.stop('final FightFrame return'); PCSX.pauseEmulator()
                    end
                end
                return true
            end)
        end
        return true
    end)
end
obs.vsync=PCSX.Events.createEventListener('GPU::Vsync',function()
    emit('GPU::Vsync',0,tonumber(regs.pc),0)
end)
PCSX.WebServer=PCSX.WebServer or {}
PCSX.WebServer.Handlers=PCSX.WebServer.Handlers or {}
PCSX.WebServer.Handlers['t3-latest-phase']=function() return obs.latest or '' end
print('T3 read-only phase capture armed; requires enabled debugger')
