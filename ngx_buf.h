
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_BUF_H_INCLUDED_
#define _NGX_BUF_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef void *            ngx_buf_tag_t;

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


struct ngx_chain_s { // 链表(放在内存池上) 内存池的pool->chain结构
    ngx_buf_t    *buf;
    ngx_chain_t  *next;
}; // 用链表的好处：如果一次需要分配的缓冲区的内存很大的时候，并不需要分配一块完整的内存，只需要将缓冲区利用链表串起来就可以了


typedef struct {
    ngx_int_t    num;
    size_t       size;
} ngx_bufs_t;


typedef struct ngx_output_chain_ctx_s  ngx_output_chain_ctx_t;

typedef ngx_int_t (*ngx_output_chain_filter_pt)(void *ctx, ngx_chain_t *in);

typedef void (*ngx_output_chain_aio_pt)(ngx_output_chain_ctx_t *ctx,
    ngx_file_t *file);

struct ngx_output_chain_ctx_s {
    ngx_buf_t                   *buf;
    ngx_chain_t                 *in;
    ngx_chain_t                 *free;
    ngx_chain_t                 *busy;

    unsigned                     sendfile:1;
    unsigned                     directio:1;
    unsigned                     unaligned:1;
    unsigned                     need_in_memory:1;
    unsigned                     need_in_temp:1;
    unsigned                     aio:1;

#if (NGX_HAVE_FILE_AIO || NGX_COMPAT)
    ngx_output_chain_aio_pt      aio_handler;
#if (NGX_HAVE_AIO_SENDFILE || NGX_COMPAT)
    ssize_t                    (*aio_preload)(ngx_buf_t *file);
#endif
#endif

#if (NGX_THREADS || NGX_COMPAT)
    ngx_int_t                  (*thread_handler)(ngx_thread_task_t *task,
                                                 ngx_file_t *file);
    ngx_thread_task_t           *thread_task;
#endif

    off_t                        alignment;

    ngx_pool_t                  *pool;
    ngx_int_t                    allocated;
    ngx_bufs_t                   bufs;
    ngx_buf_tag_t                tag;

    ngx_output_chain_filter_pt   output_filter;
    void                        *filter_ctx;
};


typedef struct {
    ngx_chain_t                 *out;
    ngx_chain_t                **last;
    ngx_connection_t            *connection;
    ngx_pool_t                  *pool;
    off_t                        limit;
} ngx_chain_writer_ctx_t;


#define NGX_CHAIN_ERROR     (ngx_chain_t *) NGX_ERROR


#define ngx_buf_in_memory(b)        (b->temporary || b->memory || b->mmap)
#define ngx_buf_in_memory_only(b)   (ngx_buf_in_memory(b) && !b->in_file)

#define ngx_buf_special(b)                                                   \
    ((b->flush || b->last_buf || b->sync)                                    \
     && !ngx_buf_in_memory(b) && !b->in_file)

#define ngx_buf_sync_only(b)                                                 \
    (b->sync                                                                 \
     && !ngx_buf_in_memory(b) && !b->in_file && !b->flush && !b->last_buf)

#define ngx_buf_size(b)                                                      \
    (ngx_buf_in_memory(b) ? (off_t) (b->last - b->pos):                      \
                            (b->file_last - b->file_pos))

ngx_buf_t *ngx_create_temp_buf(ngx_pool_t *pool, size_t size);
ngx_chain_t *ngx_create_chain_of_bufs(ngx_pool_t *pool, ngx_bufs_t *bufs);


#define ngx_alloc_buf(pool)  ngx_palloc(pool, sizeof(ngx_buf_t))
#define ngx_calloc_buf(pool) ngx_pcalloc(pool, sizeof(ngx_buf_t))

ngx_chain_t *ngx_alloc_chain_link(ngx_pool_t *pool);
#define ngx_free_chain(pool, cl)                                             \
    cl->next = pool->chain;                                                  \
    pool->chain = cl



ngx_int_t ngx_output_chain(ngx_output_chain_ctx_t *ctx, ngx_chain_t *in);
ngx_int_t ngx_chain_writer(void *ctx, ngx_chain_t *in);

ngx_int_t ngx_chain_add_copy(ngx_pool_t *pool, ngx_chain_t **chain,
    ngx_chain_t *in);
ngx_chain_t *ngx_chain_get_free_buf(ngx_pool_t *p, ngx_chain_t **free);
void ngx_chain_update_chains(ngx_pool_t *p, ngx_chain_t **free,
    ngx_chain_t **busy, ngx_chain_t **out, ngx_buf_tag_t tag);

off_t ngx_chain_coalesce_file(ngx_chain_t **in, off_t limit);

ngx_chain_t *ngx_chain_update_sent(ngx_chain_t *in, off_t sent);

#endif /* _NGX_BUF_H_INCLUDED_ */
