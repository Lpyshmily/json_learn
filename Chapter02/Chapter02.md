## Chapter02

特点：

* 把判断`true`、`false`和`null`的三个函数概括成一个。

需要注意的几个地方：

1. 如果在0后面仍然有数字，会被判定为`LEPT_PARSE_ROOT_NOT_SINGULAR`，而不是`LEPT_PARSE_INVALID_VALUE`。

2. 第一个数字为0时，type会先赋值为`LEPT_NUMBER`，如果后面仍然有数字或字符，在`lept_parse()`函数的条件分支中，应该先把type赋值为`LEPT_NULL`。

   ```c++
   if (ret == LEPT_PARSE_OK)
   	{
   		lept_parse_whitesapce(&c);
   		if (*(c.json) != '\0')
   		{
   			ret = LEPT_PARSE_ROOT_NOT_SINGULAR;
   			v->type = LEPT_NULL;
   		}
   	}
   ```

   