## Chapter 06

* 结构体相互引用：如果一个结构体类型struct1想用另一个结构体类型struct2作为它的成员，那么struct2必须在struct1之前定义；如果想用struct2的指针作为成员，那么struct2可以在struct1之后定义，但要像函数一样在struct1之前声明

  ```c++
  struct struct1
  {
      ...
  };
  
  struct struct2
  {
      struct1 s;
  };
  ```

  ```c++
  struct struct1;
  struct struct2
  {
      struct1* ps;
      ...
  }
  
  struct struct1
  {
      ...
  };
  ```

* 如果定义一个变量为指针类型，那么使用之前一定要确保这个指针指向某个地址。

  ```c++
  struct lept_member;
  
  struct lept_value
  {
  	union {
  		struct { lept_member* m; size_t msize; };
  		struct { lept_value* e; size_t size; };  // array: elements, element count
  		struct { char* s; size_t len; };         // null-terminated string, string length
  		double n;                                // number
  	};
  	lept_type type;
  };
  
  struct lept_member
  {
  	char* k; size_t klen;     // member key string, key string length
  	lept_value* v;             // member value
  };
  
  // using lept_member
  lept_member m;
  m.v->type = LEPT_NULL; // invalid
  ```

  