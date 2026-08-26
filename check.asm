section .data
	A dd 123
	msg db"Sum is %d",10,0
section .bss
	sum resd 1
section .text
	global main
	extern printf
main:
	mov esi,dword[A]
	mov byte [esi]
	mov al,[esi]
	mov eax,al
	cmp eax,0
	jz endp
	xor ecx,ecx
lp:	
	add ecx,eax
	;;inc ecx
	inc eax
	jnz lp
endp:	
 		
	push ecx
	push msg
	call printf
	add esp,8

	ret
	
