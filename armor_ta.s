	.section	.rodata.str1.8,"aMS",@progbits,1
	.align	3
.LARMORC0:
	.string	"Check 1 failed!"
	.text
	.align	2
	.p2align 3,,7
	.section	.rodata.str1.8
	.align	3
.LARMORC1:
	.string	"r"
	.align	3
.LARMORC2:
	.string	"Check 3 failed!"
	.align	3
.LARMORC3:
	.string	"Check 4 failed!"
	.text
	.align	2
	.p2align 3,,7
	.align	2
	.p2align 3,,7
	.global	__armor_light
	.type	__armor_light, %function
__armor_light:
.LARMORFB30:
	adrp x9, save_reg
    add x9, x9, :lo12:save_reg
stp x0, x1, [x9, 24]
stp x2, x3, [x9, 40]
stp x4, x5, [x9, 56]
mrs x0, nzcv
stp x30, x0, [x9, 8]
#APP
// 91 "armor.c" 1
	mrs x1, cntpct_el0
// 0 "" 2
// 92 "armor.c" 1
	mrs x3, cntfrq_el0
// 0 "" 2
#NO_APP
	mov	x2, 65536
	ldr	x4, [x9, 96]
	ldr	x0, [x9, 88]
	sub	x1, x1, x4
	lsl	x1, x1, 29
	udiv	x1, x1, x3
	add	x1, x1, 83
	cmp	x1, x2
	csel	x1, x1, x2, ls
	cmp	x0, x1
	udiv	x1, x1, x0
	bhi	.LARMOR20
	adrp	x4, __armor_bouncer_func1
	adrp	x3, __armor_bouncer_func2
	adrp	x2, __armor_bouncer_func3
	mov	w0, 0
	add	x4, x4, :lo12:__armor_bouncer_func1
	add	x3, x3, :lo12:__armor_bouncer_func2
	add	x2, x2, :lo12:__armor_bouncer_func3
	.p2align 3,,7
.LARMOR21:
#APP
// 97 "armor.c" 1
	blr x4
blr x3
blr x2
// 0 "" 2
#NO_APP
	add	w0, w0, 1
	cmp	x1, x0, uxtw
	bhi	.LARMOR21
.LARMOR20:
#APP
// 105 "armor.c" 1
	mrs x0, cntpct_el0
// 0 "" 2
#NO_APP
	str	x0, [x9, 96]
	ldp x30, x0, [x9, 8]
	msr nzcv, x0
ldp x0, x1, [x9, 24]
ldp x2, x3, [x9, 40]
ldp x4, x5, [x9, 56]
	ret
.LARMORFE30:
	.size	__armor_light, .-__armor_light
	.section	.rodata.str1.8
	.align	3
.LARMORC4:
	.string	"Armor:timeout!\nThe consumed bytes:%llu\n"
	.text
	.align	2
	.p2align 3,,7
	.global	__armor_main
	.type	__armor_main, %function
__armor_main:
.LARMORFB31:
    adrp x9, save_reg
    add x9, x9, :lo12:save_reg
	stp x0, x1, [x9, 24]
	stp x2, x3, [x9, 40]
	stp x4, x5, [x9, 56]
	mrs x0, nzcv
	stp x30, x0, [x9, 8]
	stp	x29, x30, [sp, -64]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	stp	x23, x24, [sp, 48]
//	bl	__pid_replacement
	nop
#APP
// 117 "armor.c" 1
	mrs x22, cntpct_el0
// 0 "" 2
// 118 "armor.c" 1
	mrs x4, cntfrq_el0
// 0 "" 2
#NO_APP
	adrp	x21, __armor_bouncer_func1
	adrp	x20, __armor_bouncer_func2
	adrp	x19, __armor_bouncer_func3
//	mov	w0, 1024
	mov	w0, 4096
	add	x21, x21, :lo12:__armor_bouncer_func1
	add	x20, x20, :lo12:__armor_bouncer_func2
	add	x19, x19, :lo12:__armor_bouncer_func3
	.p2align 3,,7
.LARMOR24:
#APP
// 120 "armor.c" 1
	blr x21
blr x20
blr x19
// 0 "" 2
#NO_APP
	subs	w0, w0, #1
	bne	.LARMOR24
#APP
// 128 "armor.c" 1
	mrs x0, cntpct_el0
// 0 "" 2
#NO_APP
	sub	x22, x0, x22
	adrp	x23, :got:save_reg
	lsl	x22, x22, 29
	ldr	x24, [x23, #:got_lo12:save_reg]
	udiv	x22, x22, x4
	str	x0, [x24, 96]
	lsr	x22, x22, 12
//	lsr	x22, x22, 10
	cmp	x22, 24
	bhi	.LARMOR30
//	bl	__pie_aslr
	nop
	mov	x4, 36
	sub	x22, x4, x22
	mov	x4, 65536
	mov	w0, 0
//	ldr	x3, [x21, #:got_lo12:__armor_bouncer_func1]
	str	x22, [x24, 88]
//	ldr	x2, [x20, #:got_lo12:__armor_bouncer_func2]
//	ldr	x1, [x19, #:got_lo12:__armor_bouncer_func3]
	udiv	x4, x4, x22
	.p2align 3,,7
.LARMOR26:
#APP
// 142 "armor.c" 1
	blr x21
blr x20
blr x19
// 0 "" 2
#NO_APP
	add	w0, w0, 1
	cmp	w4, w0
	bgt	.LARMOR26
#APP
// 150 "armor.c" 1
	mrs x0, cntpct_el0
// 0 "" 2
#NO_APP
	adrp x9, save_reg
	add	x9, x9, :lo12:save_reg
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	str	x0, [x9, 96]
	ldp x30, x0, [x9, 8]
	msr nzcv, x0
	ldp x0, x1, [x9, 24]
	ldp x2, x3, [x9, 40]
	ldp x4, x5, [x9, 56]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 64
	ret
.LARMOR30:
	mov	x1, x22
	adrp	x0, .LARMORC4
	add	x0, x0, :lo12:.LARMORC4
	bl	printf
	mov	x30, 0
	ret
.LARMORFE31:
	.size	__armor_main, .-__armor_main
	.global	ASLR_PATH
	.section	.rodata.str1.8
	.align	3
.LARMORC5:
	.string	"/A\002\375\364\314D\006\372\274<\372\r\374\367\007\303C\357\r\366\013\376\374\021\353\372\027\353\376\024\375\361\002\002"
	.comm	save_reg,112,8
	.section	.data.rel.LARMORocal,"aw"
	.align	3
	.type	ASLR_PATH, %object
	.size	ASLR_PATH, 8
ASLR_PATH:
	.xword	.LARMORC5
	