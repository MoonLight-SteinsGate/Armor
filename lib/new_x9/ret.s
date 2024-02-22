	.arch armv8-a
	.file	"ret.c"
	.text
	.align	16
	.global	__armor_bouncer_func1
	.type	__armor_bouncer_func1, %function
__armor_bouncer_func1:
.LFB0:
	.cfi_startproc
	ret
    nop
	.cfi_endproc
.LFE0:
	.size	__armor_bouncer_func1, .-__armor_bouncer_func1
	.align	2
	.global	__armor_bouncer_func2
	.type	__armor_bouncer_func2, %function
__armor_bouncer_func2:
.LFB1:
	.cfi_startproc
	ret
    nop
	.cfi_endproc
.LFE1:
	.size	__armor_bouncer_func2, .-__armor_bouncer_func2
	.align	2
	.global	__armor_bouncer_func3
	.type	__armor_bouncer_func3, %function
__armor_bouncer_func3:
.LFB2:
	.cfi_startproc
	ret
    nop
	.cfi_endproc
.LFE2:
	.size	__armor_bouncer_func3, .-__armor_bouncer_func3
	.ident	"GCC: (GNU) 7.3.0"
	.section	.note.GNU-stack,"",@progbits
