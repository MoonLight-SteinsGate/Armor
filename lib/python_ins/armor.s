	.arch armv8-a
	.file	"armor.c"
	.section	.rodata.str1.8,"aMS",@progbits,1
	.align	3
.LC0:
	.string	"Check 1 failed!"
	.text
	.align	2
	.p2align 3,,7
	.global	__pid_replacement
	.type	__pid_replacement, %function
__pid_replacement:
.LFB25:
	stp	x29, x30, [sp, -32]!
	mov	x29, sp
	bl	fork
	cmp	w0, 0
	blt	.L9
	bne	.L10
	ldp	x29, x30, [sp], 32
	ret
	.p2align 2,,3
.L9:
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	str	x19, [sp, 16]
	bl	puts
	mov	w0, 0
	bl	exit
.L10:
	str	x19, [sp, 16]
	mov	w19, 0
	mov	w1, w19
	bl	waitpid
	mov	w0, w19
	bl	exit
.LFE25:
	.size	__pid_replacement, .-__pid_replacement
	.section	.rodata.str1.8
	.align	3
.LC1:
	.string	"r"
	.align	3
.LC2:
	.string	"Check 3 failed!"
	.align	3
.LC3:
	.string	"Check 4 failed!"
	.text
	.align	2
	.p2align 3,,7
	.global	__pie_aslr
	.type	__pie_aslr, %function
__pie_aslr:
.LFB26:
	stp	x29, x30, [sp, -304]!
	adrp	x0, :got:ASLR_PATH
	mov	x29, sp
	ldr	x0, [x0, #:got_lo12:ASLR_PATH]
	stp	x21, x22, [sp, 32]
	add	x22, sp, 176
	stp	x19, x20, [sp, 16]
	mov	x20, x22
	mov	x19, 0
	ldr	x21, [x0]
	ldrb	w0, [x21]
	strb	w0, [sp, 176]
	b	.L12
	.p2align 2,,3
.L13:
	ldrb	w1, [x21, x19]
	ldrb	w0, [x20, -1]
	add	w0, w0, w1
	strb	w0, [x20]
.L12:
	add	x19, x19, 1
	mov	x0, x21
	add	x20, x20, 1
	bl	strlen
	cmp	x0, x19
	bhi	.L13
	mov	x0, x22
	adrp	x1, .LC1
	add	x1, x1, :lo12:.LC1
	bl	fopen
	mov	x19, x0
	cbz	x0, .L11
	mov	x2, x0
	mov	w1, 128
	add	x0, sp, 48
	bl	fgets
	cbz	x0, .L18
	mov	x0, x19
	bl	fclose
	ldrb	w1, [sp, 48]
	sub	w0, w1, #47
	add	w2, w1, 100
	mul	w0, w0, w1
	cmp	w0, w2
	beq	.L11
	adrp	x0, .LC3
	add	x0, x0, :lo12:.LC3
	bl	puts
	mov	w0, 0
	bl	exit
	.p2align 2,,3
.L11:
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x29, x30, [sp], 304
	ret
	.p2align 2,,3
.L18:
	adrp	x0, .LC2
	add	x0, x0, :lo12:.LC2
	bl	puts
	mov	w0, 0
	bl	exit
.LFE26:
	.size	__pie_aslr, .-__pie_aslr
	.align	2
	.p2align 3,,7
	.global	__armor_light
	.type	__armor_light, %function
__armor_light:
.LFB30:
	adrp x9, save_reg
    add x9, x9, :lo12:save_reg
stp x0, x1, [x9, 24]
stp x2, x3, [x9, 40]
stp x4, x5, [x9, 56]
mrs x0, nzcv
stp x30, x0, [x9, 8]
#APP
// 91 "armor.c" 1
	mrs x1, cntvct_el0
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
	bhi	.L20
	adrp	x4, __armor_bouncer_func1
	adrp	x3, __armor_bouncer_func2
	adrp	x2, __armor_bouncer_func3
	mov	w0, 0
	add	x4, x4, :lo12:__armor_bouncer_func1
	add	x3, x3, :lo12:__armor_bouncer_func2
	add	x2, x2, :lo12:__armor_bouncer_func3
	.p2align 3,,7
.L21:
#APP
// 97 "armor.c" 1
	blr x4
blr x3
blr x2
// 0 "" 2
#NO_APP
	add	w0, w0, 1
	cmp	x1, x0, uxtw
	bhi	.L21
.L20:
#APP
// 105 "armor.c" 1
	mrs x0, cntvct_el0
// 0 "" 2
#NO_APP
	str	x0, [x9, 96]
	ldp x30, x0, [x9, 8]
	msr nzcv, x0
ldp x0, x1, [x9, 24]
ldp x2, x3, [x9, 40]
ldp x4, x5, [x9, 56]
	ret
.LFE30:
	.size	__armor_light, .-__armor_light
	.section	.rodata.str1.8
	.align	3
.LC4:
	.string	"Armor:timeout!\nThe consumed bytes:%llu\n"
	.text
	.align	2
	.p2align 3,,7
	.global	__armor_main
	.type	__armor_main, %function
__armor_main:
.LFB31:
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
	bl	__pid_replacement
//	nop
#APP
// 117 "armor.c" 1
	mrs x22, cntvct_el0
// 0 "" 2
// 118 "armor.c" 1
	mrs x4, cntfrq_el0
// 0 "" 2
#NO_APP
	adrp	x21, __armor_bouncer_func1
	adrp	x20, __armor_bouncer_func2
	adrp	x19, __armor_bouncer_func3
	mov	w0, 1024
	add	x21, x21, :lo12:__armor_bouncer_func1
	add	x20, x20, :lo12:__armor_bouncer_func2
	add	x19, x19, :lo12:__armor_bouncer_func3
	.p2align 3,,7
.L24:
#APP
// 120 "armor.c" 1
	blr x21
blr x20
blr x19
// 0 "" 2
#NO_APP
	subs	w0, w0, #1
	bne	.L24
#APP
// 128 "armor.c" 1
	mrs x0, cntvct_el0
// 0 "" 2
#NO_APP
	sub	x22, x0, x22
	adrp	x23, :got:save_reg
	lsl	x22, x22, 29
	ldr	x24, [x23, #:got_lo12:save_reg]
	udiv	x22, x22, x4
	str	x0, [x24, 96]
	lsr	x22, x22, 10
	cmp	x22, 28
	bhi	.L30
	bl	__pie_aslr
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
.L26:
#APP
// 142 "armor.c" 1
	blr x21
blr x20
blr x19
// 0 "" 2
#NO_APP
	add	w0, w0, 1
	cmp	w4, w0
	bgt	.L26
#APP
// 150 "armor.c" 1
	mrs x0, cntvct_el0
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
.L30:
	mov	x1, x22
	adrp	x0, .LC4
	add	x0, x0, :lo12:.LC4
	bl	printf
	mov	w0, -123
	bl	exit
.LFE31:
	.size	__armor_main, .-__armor_main
	.global	ASLR_PATH
	.section	.rodata.str1.8
	.align	3
.LC5:
	.string	"/A\002\375\364\314D\006\372\274<\372\r\374\367\007\303C\357\r\366\013\376\374\021\353\372\027\353\376\024\375\361\002\002"
	.comm	save_reg,112,8
	.section	.data.rel.local,"aw"
	.align	3
	.type	ASLR_PATH, %object
	.size	ASLR_PATH, 8
ASLR_PATH:
	.xword	.LC5
	.text
	.align	16
	.p2align 3,,7
	.global	__armor_bouncer_func1
	.type	__armor_bouncer_func1, %function
__armor_bouncer_func1:
.LFB27:
	ret
.LFE27:
	.size	__armor_bouncer_func1, .-__armor_bouncer_func1
	.align	2
	.p2align 3,,7
	.global	__armor_bouncer_func2
	.type	__armor_bouncer_func2, %function
__armor_bouncer_func2:
.LFB36:
	.cfi_startproc
	ret
	.cfi_endproc
.LFE36:
	.size	__armor_bouncer_func2, .-__armor_bouncer_func2
	.align	2
	.p2align 3,,7
	.global	__armor_bouncer_func3
	.type	__armor_bouncer_func3, %function
__armor_bouncer_func3:
.LFB34:
	.cfi_startproc
	ret
	.cfi_endproc
.LFE34:
	.size	__armor_bouncer_func3, .-__armor_bouncer_func3
	.ident	"GCC: (GNU) 9.2.0"
	.section	.note.GNU-stack,"",@progbits
