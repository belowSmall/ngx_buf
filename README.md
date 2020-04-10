## ngx_buf基本介绍

nginx的缓冲区结构，贯穿了整个nginx。

> * 缓冲区ngx_buf_t是处理大数据块的关键结构
> * 可用于内存数据和磁盘数据
> * Nginx缓冲区，主要用来存储非常大块内存 内存池的pool->chain结构

## 定义
* off_t类型用于指示文件的偏移量，常就是long类型，其默认为一个32位的整数
* 在gcc编译中会被编译为long int类型
* 在64位的Linux系统中则会被编译为long long int，这是一个64位的整数
* 其定义在unistd.h头文件中可以查看

这里需要注意的是：
* 缓冲区主要用来存储非常大的内存块，在内存池的pool->chain结构上；
* 既可处理内存数据，也可以处理磁盘数据；
* 标志位采用了位域的方式，节省空间。
* 具体看下面的代码，写了非常详尽的注释。

```c
typedef struct ngx_buf_s  ngx_buf_t;
// Nginx缓冲区，主要用来存储非常大块内存 内存池的pool->chain结构

// off_t类型用于指示文件的偏移量，常就是long类型，其默认为一个32位的整数
// 在gcc编译中会被编译为long int类型
// 在64位的Linux系统中则会被编译为long long int，这是一个64位的整数
// 其定义在unistd.h头文件中可以查看
struct ngx_buf_s {  // 可处理内存，也可处理文件(说明既可以处理内存数据，也可以处理磁盘数据)
    u_char          *pos;         // 待处理数据的开始标记(内存数据)
    u_char          *last;        // 待处理数据的结尾标记(内存数据)
    off_t            file_pos;    // 待处理文件开始标记(文件，磁盘数据)
    off_t            file_last;   // 待处理文件结尾标记(文件，磁盘数据)

    u_char          *start;       // 缓冲区开始的指针地址   /* start of buffer */
    u_char          *end;         // 缓冲区结尾的指针地址   /* end of buffer */    
    ngx_buf_tag_t    tag;         // 缓冲区标记地址
    ngx_file_t      *file;        // 引用文件(文件对象)里面包括fd和其他信息 具体在ngx_file.h  struct ngx_file_s(ngx_file.h)
    ngx_buf_t       *shadow;      // 当这个buf完整copy了另外一个buf的所有字段的时候，那么这两个buf指向的实际上是同一块内存，或者是同一个文件的同一部分，此时这两个buf的shadow字段都是指向对方的。那么对于这样的两个buf，在释放的时候，就需要使用者特别小心，具体是由哪里释放，要提前考虑好，如果造成资源的多次释放，可能会造成程序崩溃！


    /* the buf's content could be changed */
    // 位域(节省空间)
    unsigned         temporary:1; // 标志位  为1时，内存可修改

    /*
     * the buf's content is in a memory cache or in a read only memory
     * and must not be changed
     */
    unsigned         memory:1;    // 标志位  为1时，内存只读

    /* the buf's content is mmap()ed and must not be changed */
    unsigned         mmap:1;      // 标志位  为1时，mmap映射过来的内存，不可修改

    unsigned         recycled:1;  // 标志位  为1时，可回收
    unsigned         in_file:1;   // 标志位  为1时，表示处理的是文件
    unsigned         flush:1;     // 标志位  为1时，表示需要进行flush操作
    unsigned         sync:1;      // 标志位，为1时，表示可以进行同步操作，容易引起堵塞
    unsigned         last_buf:1;  // 标志位，为1时，表示为缓冲区链表ngx_chain_t上的最后一块待处理缓冲区
    unsigned         last_in_chain:1;  // 标志位，为1时，表示为缓冲区链表ngx_chain_t上的最后一块缓冲区

    unsigned         last_shadow:1;    // 标志位，为1时，表示是否是最后一个影子缓冲区
    unsigned         temp_file:1;      // 标志位，为1时，表示当前缓冲区是否属于临时文件

    /* STUB */ int   num;
}; // 这个对象的创建，可以在内存上分配。也可以使用定义好的两个宏 (在下面有定义)
   // #define ngx_alloc_buf(pool)  ngx_palloc(pool, sizeof(ngx_buf_t))
   // #define ngx_calloc_buf(pool) ngx_pcalloc(pool, sizeof(ngx_buf_t))
```

## 缓冲区链表
这里需要注意的是：
* ngx_chain_t是用来管理ngx_buf_t；
* 内存池的pool->chain就是保存空闲的缓冲区链表的；
* 通过链表的方式实现buf有一个非常大的好处：如果一次需要缓冲区的内存很大，那么并不需要分配一块完整的内存，只需要将缓冲区串起来就可以了
```c
struct ngx_chain_s { // 链表(放在内存池上) 内存池的pool->chain结构
    ngx_buf_t    *buf;
    ngx_chain_t  *next;
}; // 用链表的好处：如果一次需要分配的缓冲区的内存很大的时候，并不需要分配一块完整的内存，只需要将缓冲区利用链表串起来就可以了
```

## 结构图
![ngx_buf.jpg](https://upload-images.jianshu.io/upload_images/18154407-9fffee5f548106e3.jpg?imageMogr2/auto-orient/strip%7CimageView2/2/w/1240)

## 函数实现
### 创建一个缓冲区
看到第一个参数，就应该想到，这个缓冲区是在内存池上创建的

第二个参数是缓冲区大小

并标记为内存可修改 ```b->temporary = 1;```
```ngx_palloc``` 是用来分配内存的，分析内存池的文章有提及
```c
ngx_buf_t *
ngx_create_temp_buf(ngx_pool_t *pool, size_t size)  // 创建一个缓冲区  @param1(在内存池上创建) @param2 缓冲区大小
{
    ngx_buf_t *b;

    // 头文件有定义(内存池中) #define ngx_calloc_buf(pool) ngx_pcalloc(pool, sizeof(ngx_buf_t))
    b = ngx_calloc_buf(pool);
    if (b == NULL) {
        return NULL;
    }

    b->start = ngx_palloc(pool, size);  // ngx_palloc.c中有实现
    if (b->start == NULL) {
        return NULL;
    }

    /*
     * set by ngx_calloc_buf():
     *
     *     b->file_pos = 0;
     *     b->file_last = 0;
     *     b->file = NULL;
     *     b->shadow = NULL;
     *     b->tag = 0;
     *     and flags
     */

    b->pos = b->start;        // 待处理数据的开始标记
    b->last = b->start;       // 待处理数据的结尾标记
    b->end = b->last + size;  // 缓冲区结尾的指针地址
    b->temporary = 1;         // 内存可修改

    return b;
}
```

### 创建缓冲区链表
必须有一种结构来管理缓冲区（最好是链表），不然无法回收
内存池上的chain字段 ，被清空的ngx_chain_t结构都会放在pool->chain缓冲链上
```c
// 必须有链表结构来管理ngx_buf_t，不然无法回收
ngx_chain_t *
ngx_alloc_chain_link(ngx_pool_t *pool)  // 创建链表结构
{
    ngx_chain_t  *cl;

    cl = pool->chain;  // 内存池上的chain字段  被清空的ngx_chain_t结构都会放在pool->chain缓冲链上

    if (cl) {
        pool->chain = cl->next;
        return cl;
    }

    // cl 为 NULL, 从内存池上分配一个ngx_chain_t结构
    cl = ngx_palloc(pool, sizeof(ngx_chain_t));
    if (cl == NULL) {
        return NULL;
    }

    return cl;
}
```

### 创建多个缓冲区
这里重点说下代码后部分的尾插法。
* ll是二级指针
* ll记录前一次循环的&cl->next （记录了这次循环的cl的next的地址）
* 下一次循环 *ll = cl 将 &cl->next 上的值改为 cl （改变了上一次循环的cl的next的地址上的值，也就是改变了上一次循环cl的next的值，就是改变了上一次循环的next，让其指向当前的cl）
* 这样就将上一次循环的next指向当前循环的cl
```c
// 创建多个buf
/*
    typedef struct {
        ngx_int_t    num;
        size_t       size;
    } ngx_bufs_t;
*/
ngx_chain_t *
ngx_create_chain_of_bufs(ngx_pool_t *pool, ngx_bufs_t *bufs)
{
    u_char       *p;
    ngx_int_t     i;
    ngx_buf_t    *b;
    ngx_chain_t  *chain, *cl, **ll;

    // @param2  分配大小 = num * size
    p = ngx_palloc(pool, bufs->num * bufs->size);
    if (p == NULL) {
        return NULL;
    }

    ll = &chain;

    for (i = 0; i < bufs->num; i++) {

        b = ngx_calloc_buf(pool);
        if (b == NULL) {
            return NULL;
        }

        /*
         * set by ngx_calloc_buf():
         *
         *     b->file_pos = 0;
         *     b->file_last = 0;
         *     b->file = NULL;
         *     b->shadow = NULL;
         *     b->tag = 0;
         *     and flags
         *
         */

        b->pos = p;
        b->last = p;
        b->temporary = 1;

        b->start = p;
        p += bufs->size;
        b->end = p;

        cl = ngx_alloc_chain_link(pool);  // 创建链表结构
        if (cl == NULL) {
            return NULL;
        }

        cl->buf = b;

        // 尾插法
        // ll记录前一次循环的&cl->next
        // 下一次循环 *ll = cl 将 &cl->next 上的值改为 cl
        // 这样就将上一次循环的next指向当前循环的cl
        *ll = cl;
        ll = &cl->next; 
    }

    *ll = NULL;

    return chain;
}
```

### 将其他缓冲区链表放到已有缓冲区链表的尾部
拿到 *chain 链表最后一个节点的地址，然后使用上面说的尾插法，一个一个地将缓冲区插入到 *chain 链表最后一个节点后
```c
// 将其他缓冲区链表放到已有缓冲区链表的尾部
ngx_int_t
ngx_chain_add_copy(ngx_pool_t *pool, ngx_chain_t **chain, ngx_chain_t *in)
{
    ngx_chain_t  *cl, **ll;

    ll = chain;

    for (cl = *chain; cl; cl = cl->next) {
        ll = &cl->next; // 拿到*chain链表最后一个节点的地址
    }

    while (in) {
        cl = ngx_alloc_chain_link(pool);
        if (cl == NULL) {
            *ll = NULL;
            return NGX_ERROR;
        }

        cl->buf = in->buf;
        *ll = cl;
        ll = &cl->next;
        in = in->next;
    }

    *ll = NULL;

    return NGX_OK;
}
```

### 获取一个空闲buf
```c
// 获取一个空闲buf
ngx_chain_t *
ngx_chain_get_free_buf(ngx_pool_t *p, ngx_chain_t **free)
{
    ngx_chain_t  *cl;

    if (*free) {
        cl = *free;
        *free = cl->next;
        cl->next = NULL;
        return cl;
    }

    cl = ngx_alloc_chain_link(p);
    if (cl == NULL) {
        return NULL;
    }

    cl->buf = ngx_calloc_buf(p);
    if (cl->buf == NULL) {
        return NULL;
    }

    cl->next = NULL;

    return cl;
}
```

### 释放缓冲区链表
释放掉的cl归还到 pool->chain 上
```c
/*
#define ngx_free_chain(pool, cl)                                             \
    cl->next = pool->chain;                                                  \
    pool->chain = cl
*/

// 释放缓冲区链表
void
ngx_chain_update_chains(ngx_pool_t *p, ngx_chain_t **free, ngx_chain_t **busy,
    ngx_chain_t **out, ngx_buf_tag_t tag)  // free 空闲的  busy 忙碌的  out 待处理的
{
    ngx_chain_t  *cl;

    // 判断out应该放到什么位置
    // 如果busy == NULL 则*busy = *out
    // 否则将*out放到*busy的末尾
    if (*out) {
        if (*busy == NULL) {
            *busy = *out;

        } else {
            for (cl = *busy; cl->next; cl = cl->next) { /* void */ }

            cl->next = *out;
        }

        *out = NULL;
    }

    // 检查busy
    // 如果busy中的buf还存在需要处理的内存空间，则停止处理
    // 否则将buf置空(处理pos last)
    while (*busy) {
        cl = *busy;

        if (ngx_buf_size(cl->buf) != 0) {
            break;
        }

        if (cl->buf->tag != tag) {
            *busy = cl->next;
            ngx_free_chain(p, cl);
            continue;
        }

        cl->buf->pos = cl->buf->start;
        cl->buf->last = cl->buf->start;

        *busy = cl->next;
        cl->next = *free;
        *free = cl;
    }
}
```

---

2020.4.10 23:09 广州
