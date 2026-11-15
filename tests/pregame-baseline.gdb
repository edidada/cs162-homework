set pagination off
set confirm off
set architecture i386
file kernel.o
target remote :1234
add-symbol-file tests/userprog/do-nothing 0x08048094
break page_fault
continue
printf "=== BASELINE PAGE FAULT ===\n"
print/x $cr2
print/x *f
x/i f->eip
disassemble _start
bt
python
assert (int(gdb.parse_and_eval('$cr2')) & 0xffffffff) >= 0xc0000000
assert int(gdb.parse_and_eval('f->cs')) == 0x1b
assert int(gdb.parse_and_eval('f->error_code')) & 4
print('BASELINE FAULT ASSERTIONS PASSED')
end
detach
quit

