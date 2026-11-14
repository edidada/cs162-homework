set pagination off
set confirm off
set architecture i386
file map
target remote :1234
break main
continue
printf "MAIN_ARGV="
print/x argv
x/s argv[0]
printf "RECUR_ADDRESS="
print/x &recur
break recur
continue
info args
info registers
disassemble /m recur
delete breakpoints
break recur if i == 0
continue
if i != 0
  quit 1
end
printf "ZERO_RECURSION_BACKTRACE\n"
backtrace
info frame
frame 4
printf "MAIN_RETURN_FRAME\n"
info frame
frame 0
delete breakpoints
finish
finish
finish
finish
printf "RETURNED_TO_MAIN\n"
continue
quit
