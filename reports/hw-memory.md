# CS162 Memory allocator

官方当前编为HW4，模板仍称hw3lib.so。实现mm_malloc/mm_free/mm_realloc：首适应、可拆分双链表、物理相邻空闲块双向合并、返回前清零、0/NULL及大小溢出处理、sbrk失败不改变链表；realloc先分配后复制释放，失败保留原块。C99目标使用联合体保证基本类型对齐，容量按header大小取整。非线程安全，不支持无效指针/重复free，与其他sbrk用户并发操作不受支持。

原header/Makefile/mm_test不变。远端测试覆盖原测试、20000随机操作、数据保留/清零/对齐/拆分/双向合并/复用不扩堆/溢出失败。UBSan为附加验证；不以ASan替代自定义sbrk堆边界检查。本地未编译执行。

Pintos sbrk内核部分尚未实现，不能称整个Memory作业完成。官方：https://cs162.org/static/hw/hw-memory/docs/library-functions/memory-allocator/ 。
