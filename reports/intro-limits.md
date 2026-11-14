# CS162 HW0 Limits / GDB

limits通过getrlimit读取STACK/NPROC/NOFILE的rlim_cur软限制；保持三个输出标签。uintmax_t打印有限值，RLIM_INFINITY用-1表示，失败perror并非零退出。仅Makefile添加-DCS162，不改map/printer源码，满足预处理练习。

远端32位构建、父进程resource模块独立比对继承软限制，再仅对子进程下调三个soft值比对；不修改宿主全局限制。保留ELF、汇编、符号表、预处理及实际map输出。GDB使用原i386-gdb-map.sh规定路径/清理环境，通过自建i386-gdb适配器运行qemu-i386远程stub和gdb-multiarch批处理。保留argv/函数地址、寄存器、混合反汇编、i==0条件断点、四层recur+main调用栈、main返回frame和逐层finish证据，验证3/2/1/0参数地址递减。

适配器不等价于课程原i386工具包，地址/编译器版本与课程容器可能不同；batch没有交互TUI布局切换。书面题须依据真实日志补写，校内Gradescope/私有grader不在当前范围。原map的演示分配不释放，未将它当生产内存安全程序。无本地编译运行、无新增Python源文件，尚待最终远端结论。
