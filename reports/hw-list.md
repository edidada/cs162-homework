# CS162 HW1 Pintos Lists

lwords通过Pintos list API完成初始化/查找/所有权插入/输出/通用排序，比较器通过aux转接，不能写死频率比较。add_word接管字符串，重复时释放多余字符串；新节点分配失败也释放输入。

pwords每文件pthread，线程内打开/扫描/关闭，调用count_words外不持锁；共享列表内每个方法以mutex保护，add_word使用内部无锁查找避免重入死锁。所有join完成后排序/输出/释放并销毁锁。find_word返回借用节点，调用者须在join后读取变化字段，接口不提供并发快照。线程创建失败时join已启动线程并清理，文件打开失败非零返回。

原list.c/h、word_count.h、word_helpers与预编译对象、Makefile保持。CI原targets、普通CLI oracle和ASan/UBSan独立API测试，含通用反向比较器、重复/所有权、8线程4000共享更新、Gutenberg/多文件/反复并发与时间采样。ASan/UBSan非race detector，原object不插桩，原lwords driver不释放由课程提供；实际性能待日志分析。尚待远端结论，pthread书面题/私有grader未完成。本地不编译运行，无新增Python源文件。
