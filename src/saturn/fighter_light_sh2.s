/* The SH-2 routines of fighter_light.c: the relight of a fighter's Gouraud tables. The data is
 * scripts/build_fighter_packs.py's lighting_data; fighter_light.c states the job and calls these.
 *
 * A normal's level is its dot product with the part's light row, in 1/1024, rounded and held to
 * 0..15; its fill level the same with the fill row, in 1/2048, held to 0..7. The level byte is
 * (level * 8 + fill) * 2, the offset of its Gouraud word in a tone's row of the shade table; bit
 * 0 marks a normal that its last walk changed.
 *
 * Only a changed normal costs more than its dot products: its entries get their colours and the
 * faces that one entry lights get their tables. An entry that a shaded face reads keeps its
 * colour in the stream, bit 15 set while it is new; the walk after clears that bit, and the
 * shaded routines write the faces that have such a corner. */

/* uint32_t t3_fighter_light_part_sh2(const light_part_job_t* job)
 * Registers through the loop:
 *   r4 stream   r5 the normal's level byte   r6 normals left
 *   r8 r9 r10 the light row   r11 r12 r13 the fill row   r14 shade table (the tables follow)
 *   r1 the new level byte   r2 words of entries, then their end   r0 r3 r7 scratch
 * Stack: @(0) all   @(4) normals changed */
	.section .text.t3_fighter_light_part_sh2,"ax",@progbits
	.align 2
	.global _t3_fighter_light_part_sh2
	.type _t3_fighter_light_part_sh2,@function
_t3_fighter_light_part_sh2:
	mov.l r8,@-r15
	mov.l r9,@-r15
	mov.l r10,@-r15
	mov.l r11,@-r15
	mov.l r12,@-r15
	mov.l r13,@-r15
	mov.l r14,@-r15
	add #-8,r15
	mov.l @(20,r4),r0
	mov.l @(8,r4),r1
	mov.l r0,@r15
	mov #0,r0
	mov.l r0,@(4,r15)
	mov.w @r1+,r8
	mov.w @r1+,r9
	mov.w @r1+,r10
	mov.w @r1+,r11
	mov.w @r1+,r12
	mov.w @r1+,r13
	mov.l @(12,r4),r5
	mov.l @(16,r4),r14
	mov.l @(4,r4),r6
	mov.l @r4,r4
.Llp_normal:
	/* Both dot products: r3 the light's, r0 the fill's. */
	mov.b @r4+,r0
	mov.b @r4+,r1
	muls.w r8,r0
	mov.b @r4+,r2
	sts macl,r3
	muls.w r11,r0
	sts macl,r0
	muls.w r9,r1
	sts macl,r7
	muls.w r12,r1
	add r7,r3
	sts macl,r7
	muls.w r10,r2
	add r7,r0
	sts macl,r7
	muls.w r13,r2
	add r7,r3
	sts macl,r7
	add r7,r0
	/* The level, 0 for a normal turned away. */
	cmp/pl r3
	bf/s .Llp_lit
	mov #0,r1
	shlr8 r3
	shlr r3
	add #1,r3
	shlr r3
	mov #15,r1
	cmp/hi r1,r3
	bt .Llp_lit
	mov r3,r1
.Llp_lit:
	cmp/pl r0
	bf/s .Llp_fill
	mov #0,r2
	shlr8 r0
	shlr2 r0
	add #1,r0
	shlr r0
	mov #7,r2
	cmp/hi r2,r0
	bt .Llp_fill
	mov r0,r2
.Llp_fill:
	shll2 r1
	shll r1
	add r2,r1
	mov.b @r5,r0
	shll r1
	mov.b @r4+,r2
	mov r0,r3
	and #0xFE,r0
	extu.b r2,r2
	cmp/eq r1,r0
	bf .Llp_changed
	mov.l @r15,r0
	tst r0,r0
	bf .Llp_changed
	mov r3,r0
	tst #1,r0
	bf .Llp_clear
	/* Unchanged, as its last walk left it: past its entries. */
	add r2,r4
	add r2,r4
.Llp_next:
	dt r6
	bf/s .Llp_normal
	add #1,r5
	mov.l @(4,r15),r0
	add #8,r15
	mov.l @r15+,r14
	mov.l @r15+,r13
	mov.l @r15+,r12
	mov.l @r15+,r11
	mov.l @r15+,r10
	mov.l @r15+,r9
	rts
	mov.l @r15+,r8

	/* A new level: marked, and each of its entries takes its colour. */
.Llp_changed:
	mov r1,r0
	or #1,r0
	mov.b r0,@r5
	mov.l @(4,r15),r0
	add #1,r0
	mov.l r0,@(4,r15)
	tst r2,r2
	bt .Llp_next
	add r2,r2
	add r4,r2
.Llp_entry:
	/* The head is tone << 8 | kept | faces: the tone's row, then the level's word in it. */
	mov.w @r4+,r0
	mov r0,r7
	mov r0,r3
	shlr8 r3
	shll8 r3
	add r14,r3
	mov r1,r0
	mov.w @(r0,r3),r3
	mov r7,r0
	tst #0x80,r0
	bt .Llp_faces
	mov.w .Llp_new,r7
	or r3,r7
	mov.w r7,@r4
	add #2,r4
.Llp_faces:
	and #0x7F,r0
	tst r0,r0
	bt .Llp_entry_end
	/* The faces this entry alone lights: the colour at all four corners. */
	mov r3,r7
	shll16 r7
	or r3,r7
	mov r0,r3
.Llp_face:
	mov.w @r4+,r0
	mov.l r7,@(r0,r14)
	add #4,r0
	dt r3
	bf/s .Llp_face
	mov.l r7,@(r0,r14)
.Llp_entry_end:
	cmp/hs r2,r4
	bf .Llp_entry
	bra .Llp_next
	nop

	/* The same level as its last walk gave it, which marked it: the marks go. */
.Llp_clear:
	mov.b r1,@r5
	tst r2,r2
	bt .Llp_next
	add r2,r2
	add r4,r2
	mov.w .Llp_colour,r7
.Llp_clear_entry:
	mov.w @r4+,r0
	tst #0x80,r0
	bt .Llp_clear_faces
	mov.w @r4,r3
	and r7,r3
	mov.w r3,@r4
	add #2,r4
.Llp_clear_faces:
	and #0x7F,r0
	add r0,r4
	add r0,r4
	cmp/hs r2,r4
	bf .Llp_clear_entry
	bra .Llp_next
	nop
	.align 1
.Llp_new:
	.word 0x8000
.Llp_colour:
	.word 0x7FFF
	.size _t3_fighter_light_part_sh2,.-_t3_fighter_light_part_sh2

/* void t3_fighter_light_shaded{4,3}_sh2(const uint16_t* faces, uint32_t count,
 *                                       const uint8_t* stream, volatile uint8_t* shade)
 * A face is its table's offset and the stream offsets of its corners' colours: four, or three
 * with corner D taking C's. Registers:
 *   r4 faces   r5 faces left   r6 stream   r7 shade table   r10 the colour bits of a table word
 *   r1 table offset   r2 r3 r8 r9 corners A B C D */
.macro shaded corners
	.section .text.t3_fighter_light_shaded\corners\()_sh2,"ax",@progbits
	.align 2
	.global _t3_fighter_light_shaded\corners\()_sh2
	.type _t3_fighter_light_shaded\corners\()_sh2,@function
_t3_fighter_light_shaded\corners\()_sh2:
	tst r5,r5
	bt .Lls\corners\()_none
	mov.l r8,@-r15
	mov.l r9,@-r15
	mov.l r10,@-r15
	mov.l .Lls\corners\()_colours,r10
.Lls\corners\()_face:
	mov.w @r4+,r1
	mov.w @r4+,r0
	mov.w @(r0,r6),r2
	mov.w @r4+,r0
	mov.w @(r0,r6),r3
	mov.w @r4+,r0
	mov.w @(r0,r6),r8
	.if \corners == 4
	mov.w @r4+,r0
	mov.w @(r0,r6),r9
	.else
	mov r8,r9
	.endif
	/* Bit 15 of a corner: its colour is new. */
	mov r2,r0
	or r3,r0
	or r8,r0
	.if \corners == 4
	or r9,r0
	.endif
	cmp/pz r0
	bt .Lls\corners\()_next
	shll16 r2
	extu.w r3,r3
	or r3,r2
	shll16 r8
	extu.w r9,r9
	or r9,r8
	and r10,r2
	and r10,r8
	mov r1,r0
	mov.l r2,@(r0,r7)
	add #4,r0
	mov.l r8,@(r0,r7)
.Lls\corners\()_next:
	dt r5
	bf .Lls\corners\()_face
	mov.l @r15+,r10
	mov.l @r15+,r9
	mov.l @r15+,r8
.Lls\corners\()_none:
	rts
	nop
	.align 2
.Lls\corners\()_colours:
	.long 0x7FFF7FFF
	.size _t3_fighter_light_shaded\corners\()_sh2,.-_t3_fighter_light_shaded\corners\()_sh2
.endm

	shaded 4
	shaded 3
	.text
