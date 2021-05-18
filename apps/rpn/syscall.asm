bits 64
section .text

global SyscallLogString
SyscallLogString:
  mov eax, 0x80000000 ; system call number
  mov r10, rcx ; rcx is used in syscall, so store the value of rcx in r10
  syscall
  ret

global SyscallPutString
SyscallPutString:
  mov eax, 0x80000001
  mov r10, rcx
  syscall
  ret
