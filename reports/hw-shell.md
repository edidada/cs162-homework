# CS162 HW2 Shell

官方要求：https://cs162.org/static/hw/hw-shell/ 。实现目录内建、execv及手动PATH搜索、双向重定向、动态多级管道、独立作业进程组和前台终端交接，以及加分项后台、wait、fg/bg。

原tokenizer、头文件、Makefile和test.sh保持。语法边界沿用tokenizer：不额外增加变量替换、通配符、分号、追加或stderr重定向；特殊符号须空格分隔。tokenizer不保留引号来源，因此被引号包围的单独特殊符号也无法与操作符区分。最大输入沿用4096字节，超长行拒绝并排空。

管道成员全部fork后才wait，父子关闭无用端点；启动门阻止子进程在终端转交前读取或在其余成员加入前退出。父子均setpgid减少竞争；shell忽略交互信号，子进程恢复默认行为。作业以整条管道为单位，共享进程组，记录每个成员的退出/暂停状态和termios。前台完成或全员停止后恢复shell终端，fg恢复作业termios。后台子进程在下次读取命令前及wait/fg/bg时回收，不在信号处理器里分配内存。

退出对剩余作业发送HUP及CONT；wait遇暂停作业报告并返回，避免永远等待不能自行继续的进程。内建命令只在单阶段由shell执行；不要求其重定向或管道执行。未实施完整POSIX shell语义。

独立GitHub Actions运行原测试、CLI比较、超管道容量生产者/提前退出消费者、多级管道/重复执行、真实PTY Ctrl-C/Ctrl-Z/fg/bg/后台终端读取/嵌套shell/termios恢复；ASan/UBSan运行非交互检查。尚未取得远端终态；私有grader未执行。本地不编译或执行。
