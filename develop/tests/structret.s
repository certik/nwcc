	.file	"structret.c"
	.section	.rodata.str1.1,"aMS",@progbits,1
.LC0:
	.string	"hello"
	.text
	.p2align 4,,15
.globl func
	.type	func, @function
func:
	pushl	%ebp
	movl	%esp, %ebp
	movl	8(%ebp), %eax
	movl	$123, %edx
	movl	$.LC0, %ecx
	movl	%edx, (%eax)
	movl	%ecx, 4(%eax)
	popl	%ebp
	ret	$4
	.size	func, .-func
	.p2align 4,,15
.globl func2
	.type	func2, @function
func2:
	pushl	%ebp
	movl	%esp, %ebp
	movl	8(%ebp), %eax
	movl	12(%ebp), %ecx
	movl	%ecx, (%eax)
	popl	%ebp
	ret	$4
	.size	func2, .-func2
	.section	.rodata.str1.1
.LC1:
	.string	"results of func: %d,%s\n"
.LC2:
	.string	"results of func2: %d %d\n"
	.text
	.p2align 4,,15
.globl main
	.type	main, @function
main:
	pushl	%ebp
	movl	%esp, %ebp
	subl	$8, %esp
	andl	$-16, %esp
	movl	$.LC0, %edx
	pushl	%ecx
	pushl	%edx
	movl	$123, %eax
	pushl	%eax
	pushl	$.LC1
	call	printf
	addl	$12, %esp
	pushl	$1
	pushl	$-56
	pushl	$.LC2
	call	printf
	xorl	%eax, %eax
	leave
	ret
	.size	main, .-main
	.ident	"GCC: (GNU) 3.3 20030226 (prerelease) (SuSE Linux)"
