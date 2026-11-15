set pagination off
set confirm off
set architecture i386
file kernel.o
target remote :1234
break process_execute
continue
printf "=== PROCESS_EXECUTE ===\n"
print file_name
print *(struct thread *)((unsigned int)$esp & ~4095)
bt
break start_process
continue
printf "=== START_PROCESS ===\n"
print *(struct thread *)((unsigned int)$esp & ~4095)
bt
break intr_exit if ((struct intr_frame *)$esp)->cs == 0x1b
continue
printf "=== INTERRUPT_FRAME ===\n"
print/x *(struct intr_frame *)$esp
add-symbol-file tests/userprog/do-nothing 0x08048094
hbreak *_start
continue
printf "=== USER_ENTRY ===\n"
info registers
x/5xw $esp
python
assert int(gdb.parse_and_eval('$cs')) == 0x1b
assert int(gdb.parse_and_eval('$esp')) == 0xbfffffec
code = gdb.selected_frame().architecture().disassemble(int(gdb.parse_and_eval('&exit')), count=32)
trap = next(i['addr'] for i in code if 'int' in i['asm'] and '0x30' in i['asm'])
gdb.execute('hbreak *' + hex(trap))
end
continue
printf "=== BEFORE_INT_30 ===\n"
x/2xw $esp
python
assert int(gdb.parse_and_eval('*(unsigned int *)$esp')) == 1
assert int(gdb.parse_and_eval('*((unsigned int *)$esp+1)')) == 162
end
break syscall_handler
continue
printf "=== SYSCALL_HANDLER ===\n"
print/x *f
x/2xw f->esp
python
assert int(gdb.parse_and_eval('*(unsigned int *)f->esp')) == 1
assert int(gdb.parse_and_eval('*((unsigned int *)f->esp+1)')) == 162
print('GDB ASSERTIONS PASSED: user mode, entry stack, SYS_EXIT=1, status=162')
end
detach
quit

