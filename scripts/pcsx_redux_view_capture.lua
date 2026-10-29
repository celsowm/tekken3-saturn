T3_PAUL_SESSION_OPTIONS = {neutral=true}
assert(loadfile('scripts/pcsx_redux_paul_session.lua'))()
local mem=PCSX.getMemPtr()
local function u32(a)
 local p=a-0x80000000
 return tonumber(mem[p])+256*tonumber(mem[p+1])+65536*tonumber(mem[p+2])+16777216*tonumber(mem[p+3])
end
local seen=0
T3_PARITY_VIEW_LISTENER=PCSX.Events.createEventListener('GPU::Vsync',function()
 if u32(0x800AE204)==8 then
  seen=seen+1
  if seen==360 then
   print(string.format('T3_VIEW_PAUSE vsync=%d fight_tick=%d',seen,u32(0x800AFA4C)))
   PCSX.pauseEmulator()
  end
 end
end)
