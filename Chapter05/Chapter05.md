## Chapter05

解析数组

数组中的每个元素当做一个`lept_value`类型的值

`lept_parse_array()`解析方法：

* 遇到`[`后，调用`lept_parse_value()`，解析得到一个`lept_value`元素，存放在`stack`中
* 遇到逗号跳过，继续调用`lept_parse_value()`函数，直到遇到`]`
* 最后，将所有的元素一次性取出，放置到为这个数组申请的内存中去
* 如果中间没有正确解析出现错误，在返回错误标识之前，需要将已经写入`stack`的元素释放