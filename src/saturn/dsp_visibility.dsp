; Exact primary world-normal predicate for a resident vertex tile.
; MD0/1/2[0..47]: signed q = world / 6, SoA X/Y/Z. Tile span per axis <=32767.
; MD3[0..15]: packed A/B/C/D byte indices, count 1..16, indices <vertex_count.
; MD2[48..50]: original world-space eye X/Y/Z, each abs < 2^19.
; MD2[61..62]: 0x02000000 bias, 0xFC000000 range mask.
; MD0[51..57] input: 0,count,0,1,32,0,0. Outputs: visible[53], fallback[57].
; Accepted normals are in [-2^25,2^25-1]; |view| < 2^20 proves every dot
; partial fits signed 48 bits. O is an extra error check, not the bound proof.
; A zero primary normal sets fallback; CPU retains all collapsed-corner rules.
; All input vertices are preserved. MD3[32..43] holds per-axis u,v,view,temp.

FACE:   MOV 56,CT0
        MOV 0,MC0
        MOV 51,CT0
        MOV M0,CT3
        MOV M3,A
        MOV 48,CT0
        MOV ALL,MC0
        MOV 32,MC0
        MOV 32,CT3
        MVI 3,LOP
        MOV ((GATHER ^ 128)-128),TOP
GATHER: MOV 48,CT0
        MOV M0,A
        RL8 MOV ALU,A MOV ALL,MC0
        MVI 255,PL
        AND MOV ALU,A MOV ALL,CT0
        MOV ALL,CT1
        MOV ALL,CT2
        MOV M0,MC3
        MOV 49,CT0
        MOV M0,A
        MVI 4,PL
        ADD MOV ALU,A MOV ALL,CT3
        MOV M1,MC3
        ADD MOV ALU,A MOV ALL,CT3
        MOV M2,MC3
        MOV M0,A
        MVI 1,PL
        ADD MOV ALU,A MOV ALL,MC0
        MOV 49,CT0
        MOV M0,CT3
        BTM
        NOP

        MOV 55,CT0
        MOV 32,MC0
        MOV 32,CT3
        MOV 48,CT2
        MVI 2,LOP
        MOV ((COORD ^ 128)-128),TOP
COORD:  MOV 48,CT1
        MOV MC3,A
        MOV ALL,MC1
        MOV MC3,P
        SUB MOV ALU,A MOV ALL,MC1
        ADD MOV ALU,A
        ADD MOV ALU,A
        MOV MC3,P
        ADD MOV ALU,A MOV MC3,P
        MOV ALL,MC1
        MOV 48,CT1
        MOV M1,A
        SUB MOV ALU,A
        MOV 55,CT0
        MOV M0,CT3
        MOV ALL,MC3
        MOV 49,CT1
        MOV M1,MC3
        MOV 50,CT1
        MOV M1,A
        ADD MOV ALU,A
        SL MOV ALU,A
        MOV ALL,PL
        SL MOV ALU,A
        ADD MOV ALU,A
        JMP NS,CENTER_POS
        NOP
        MVI 3,PL
        ADD MOV ALU,A
CENTER_POS:
        SR MOV ALU,A
        SR MOV ALU,A
        MOV ALL,PL
        MOV MC2,A
        SUB MOV ALU,A MOV ALL,MC3
        MOV M0,A
        MVI 4,PL
        ADD MOV ALU,A MOV ALL,MC0
        MOV ALL,CT3
        BTM
        NOP

; primary cross component 0
        MOV 36,CT3
        MOV M3,X
        MOV 41,CT3
        MOV M3,Y
        CLR A MOV MUL,P
        MOV 40,CT3
        AD2 MOV ALU,A MOV M3,X
        MOV 37,CT3
        MOV M3,Y
        MOV MUL,P
        SUB MOV ALU,A
        MOV 60,CT0
        MOV ALL,MC0
        MOV 61,CT2
        MOV M2,P
        ADD MOV ALU,A
        MOV 56,CT0
        MOV M0,P
        OR MOV ALU,A MOV ALL,MC0

; primary cross component 1
        MOV 40,CT3
        MOV M3,X
        MOV 33,CT3
        MOV M3,Y
        CLR A MOV MUL,P
        MOV 32,CT3
        AD2 MOV ALU,A MOV M3,X
        MOV 41,CT3
        MOV M3,Y
        MOV MUL,P
        SUB MOV ALU,A
        MOV 60,CT1
        MOV ALL,MC1
        MOV 61,CT2
        MOV M2,P
        ADD MOV ALU,A
        MOV 56,CT0
        MOV M0,P
        OR MOV ALU,A MOV ALL,MC0

; primary cross component 2
        MOV 32,CT3
        MOV M3,X
        MOV 37,CT3
        MOV M3,Y
        CLR A MOV MUL,P
        MOV 36,CT3
        AD2 MOV ALU,A MOV M3,X
        MOV 33,CT3
        MOV M3,Y
        MOV MUL,P
        SUB MOV ALU,A
        MOV 60,CT2
        MOV ALL,MC2
        MOV 61,CT2
        MOV M2,P
        ADD MOV ALU,A
        MOV 56,CT0
        MOV M0,P
        OR MOV ALU,A MOV ALL,MC0

        MOV 56,CT0
        MOV M0,A
        MOV 62,CT2
        MOV M2,P
        AND MOV ALU,A
        JMP NZ,FALLBACK
        NOP

        MOV 60,CT0
        MOV M0,A
        MOV 60,CT1
        MOV M1,P
        OR MOV ALU,A
        MOV 60,CT2
        MOV M2,P
        OR MOV ALU,A
        JMP Z,FALLBACK
        NOP

; Load stored signed 32-bit normals again: SUB changes only ACL, so using
; its old ACH for a subsequent 48-bit product would be incorrect.
        MOV M0,X
        MOV 34,CT3
        MOV M3,Y
        CLR A MOV MUL,P
        MOV 60,CT1
        AD2 MOV ALU,A MOV M1,X
        MOV 38,CT3
        MOV M3,Y
        MOV MUL,P
        MOV 60,CT2
        AD2 MOV ALU,A MOV M2,X
        MOV 42,CT3
        MOV M3,Y
        MOV MUL,P
        AD2 MOV ALU,A
        JMP S,NEXT
        NOP
        JMP Z,NEXT
        NOP
        MOV 53,CT0
        MOV MC0,A
        MOV M0,P
        OR MOV ALU,A
        MOV 53,CT0
        MOV ALL,MC0
        JMP NEXT
        NOP
FALLBACK:
        MOV 54,CT0
        MOV M0,P
        MOV 57,CT0
        MOV M0,A
        OR MOV ALU,A MOV ALL,MC0
NEXT:   MOV 54,CT0
        MOV M0,A
        SL MOV ALU,A MOV ALL,MC0
        MOV 51,CT0
        MOV M0,A
        MVI 1,PL
        ADD MOV ALU,A MOV ALL,MC0
        MOV M0,P
        SUB MOV ALU,A
        JMP NZ,FACE
        NOP
        ENDI
