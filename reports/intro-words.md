# CS162 HW0 Words

按官方词定义扫描一次输入，连续字母长度>1才计数；大小写归一，EOF也提交最后一个词，stdin不seek/rewind。支持多文件累计、频率+字母排序、错误退出与链表释放。原wc_sort.o、头文件和Makefile保持。64字符buffer留终止符，超出声明上限的词在频率模式报错，避免截断为伪造多个词；计数模式仅记录长度是否>=2。

CI运行原构建和ASan/UBSan构建，对空/单字母/末尾无换行/大小写/标点数字/64长度/高位字节/NUL、固定seed随机文本、原Gutenberg语料逐字节比较独立正则/Counter oracle，另验多文件及缺失文件/无效参数/超长词。无本地编译运行，无新增Python源文件。尚待远端结论；HW0 limits/地址空间/GDB/书面问答及私有autograder未完成，不能宣称整个HW0通过。
