
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


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

// 合并in链中与第一个节点相邻的文件buf 并且合并长度限制在limit范围内
off_t
ngx_chain_coalesce_file(ngx_chain_t **in, off_t limit)
{
    off_t         total, size, aligned, fprev;
    ngx_fd_t      fd;
    ngx_chain_t  *cl;

    total = 0;

    cl = *in;
    fd = cl->buf->file->fd;  // 拿到文件fd

    do {
        size = cl->buf->file_last - cl->buf->file_pos; // 文件大小

        if (size > limit - total) {
            size = limit - total;

            // ngx_pagesize - 1 的 二进制为 12 个 1
            // ~(ngx_pagesize - 1) 则为 12 个 0
            // 原值 & 12 个 0 :  把原值的二进制的后12位全部置0
            // 结果：返回值 <= 原值
            aligned = (cl->buf->file_pos + size + ngx_pagesize - 1)
                       & ~((off_t) ngx_pagesize - 1);  // 一个内存池分配的最大容量(ngx_pagesize - 1)  ngx_pagesize是一块内存页的大小，在x86下通常为4096

            if (aligned <= cl->buf->file_last) {
                size = aligned - cl->buf->file_pos;
            }

            total += size;
            break;
        }

        total += size;
        fprev = cl->buf->file_pos + size;
        cl = cl->next;

    } while (cl
             && cl->buf->in_file
             && total < limit
             && fd == cl->buf->file->fd
             && fprev == cl->buf->file_pos);

    *in = cl;

    return total;
}

// 当发送了send字节之后，需要对当前使用的缓冲区做处理，并返回当前还未处理的缓冲区
ngx_chain_t *
ngx_chain_update_sent(ngx_chain_t *in, off_t sent)
{
    off_t  size;

    for ( /* void */ ; in; in = in->next) {

        if (ngx_buf_special(in->buf)) { // buf里面的内容是否只是内存里，而没有在文件里
            continue;
        }

        if (sent == 0) {
            break;
        }

        size = ngx_buf_size(in->buf); // 返回该buf的数据大小，无论是内存还是文件

        if (sent >= size) {
            sent -= size;

            if (ngx_buf_in_memory(in->buf)) {  // 是否在内存中
                in->buf->pos = in->buf->last;
            }

            if (in->buf->in_file) {  // 在文件中
                in->buf->file_pos = in->buf->file_last;
            }

            continue;
        }

        if (ngx_buf_in_memory(in->buf)) {
            in->buf->pos += (size_t) sent;
        }

        if (in->buf->in_file) {
            in->buf->file_pos += sent;
        }

        break;
    }

    return in;
}
