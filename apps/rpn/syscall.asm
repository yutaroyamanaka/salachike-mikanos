bits 64
section .text

%macro define_syscall 2
global Syscall%1
Syscall%1:
  mov rax, %2 ; system call number
  mov r10, rcx ; rcx is used in syscall, so store the value of rcx in r10
  syscall
  ret
%endmacro

define_syscall LogString, 0x80000000
define_syscall PutString, 0x80000001
define_syscall Exit, 0x80000002
