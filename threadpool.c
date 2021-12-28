#define _GNU_SOURCE
#include "threadpool.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#define MAXT_IN_POOL 200


threadpool* create_threadpool(int num_threads_in_pool){
    if(num_threads_in_pool <= 0 || num_threads_in_pool > MAXT_IN_POOL){
        printf("Usage: threadpool <pool-size> <max-number-of-jobs>\n");
        return NULL;
    }
    threadpool *pool = (threadpool*)malloc(sizeof(threadpool));
    if(!pool){
        printf("Cannot allocate memory.\n");
        return NULL;
    }
    pool->threads = (pthread_t*)malloc(sizeof(pthread_t)*num_threads_in_pool);
    if(!pool->threads){
        printf("Cannot allocate memory.\n");
        return NULL;
    }
    
    pool->num_threads = num_threads_in_pool;
    pool->qsize = 0;
    pool->qhead = NULL;
    pool->qtail = NULL;
    if((pthread_cond_init(&(pool->q_not_empty), NULL) != 0) || (pthread_cond_init(&(pool->q_empty), NULL) != 0)){
        printf("Cannot init condition.\n");
        return NULL;
    }
    if(pthread_mutex_init(&(pool->qlock), NULL) != 0){
        printf("Cannot init mutex.\n");
        return NULL;
    }
    pool->dont_accept = 0;//1 if started destruction
    pool->shutdown = 0;//1 if No more missions and will not be
    memset(pool->threads, 0,num_threads_in_pool);
    for(int i = 0; i < num_threads_in_pool; i++){
        if(pthread_create(&pool->threads[i], NULL, do_work, pool) != 0){
            printf("Cannot create thread %d", i);
            return NULL;
        }
    }
    return pool;
}

void dispatch(threadpool* from_me, dispatch_fn dispatch_to_here, void *arg){
    if(!from_me || !dispatch_to_here || !arg){
        printf("Usage: threadpool <pool-size> <max-number-of-jobs>\n");
        return;
    }
    if(from_me->dont_accept == 1)
        return;
    int fail = 0;
    fail = pthread_mutex_lock(&from_me->qlock);
    if(fail != 0){
        printf("Lock failed.\n");
        return;
    }
    work_t *new = (work_t*)malloc(sizeof(work_t));
    if(!new){
        printf("Cannot allocate memory.\n");
        return;
    }
    new->routine = dispatch_to_here;
    new->arg = arg;
    new->next = NULL;
    if(!from_me->qhead){
        from_me->qhead = new;
        from_me->qtail = new;
    }else if(from_me->qhead == from_me->qtail){
        from_me->qtail = new;
        from_me->qhead->next = from_me->qtail;
    }
    else{
        from_me->qtail->next = new;
        from_me->qtail = new;
    }
    from_me->qsize += 1;
    fail = pthread_cond_signal(&from_me->q_not_empty);
    if(fail != 0){
        printf("Signal failed.\n");
        return;
    }
    fail = pthread_mutex_unlock(&from_me->qlock);
    if(fail != 0){
        printf("Unlock failed.\n");
        return;
    }
}

void* do_work(void* p){
    if(!p){
        printf("Wrong arguments.\n");
        return NULL;
    }
    threadpool *pool = (threadpool*)p;
    while(1){
        int fail = 0;
        fail = pthread_mutex_lock(&pool->qlock);
        if(fail != 0){
            printf("Lock failed.\n");
            return NULL;
        }
        if(pool->shutdown == 1){
            fail = pthread_mutex_unlock(&pool->qlock);
            if(fail != 0){
                printf("Unlock failed.\n");
                return NULL;
            }
            return NULL;
        }
        if(pool->qsize == 0){
            fail = pthread_cond_wait(&(pool->q_not_empty), &(pool->qlock));
            if(fail != 0){
                printf("Wait failed.\n");
                return NULL;
            }
        }
        
        if(pool->shutdown == 1){
            fail = pthread_mutex_unlock(&(pool->qlock));
            if(fail != 0){
                printf("Unlock failed.\n");
                return NULL;
            }
            return NULL;
        }
        
        work_t *cur = pool->qhead;
        if(cur == NULL){
            fail = pthread_mutex_unlock(&pool->qlock);
            if(fail != 0){
                printf("Unlock failed.\n");
                return NULL;
            }
            continue;
        }
        pool->qsize--;
        pool->qhead = pool->qhead->next;
        if(!pool->qhead && pool->dont_accept == 1){
            pool->qtail = NULL;
            fail = pthread_cond_signal(&(pool->q_empty));
            if(fail != 0){
                printf("Signal failed.\n");
                return NULL;
            }
        }
        fail = pthread_mutex_unlock(&(pool->qlock));
        if(fail != 0){
            printf("Unlock failed.\n");
            return NULL;
        }
        cur->routine(cur->arg);
        free(cur);
    }
}

void destroy_threadpool(threadpool* destroyme){
    if(!destroyme){
        printf("Wrong arguments.\n");
        return;
    }
    int fail = 0;
    fail = pthread_mutex_lock(&destroyme->qlock);
    if(fail != 0){
        printf("Lock failed.\n");
        return;
    }
    destroyme->dont_accept = 1;
    fail = pthread_cond_wait(&destroyme->q_empty, &destroyme->qlock);
    if(fail != 0){
        printf("Wait failed.\n");
        return;
    }
    destroyme->shutdown = 1;
    fail = pthread_cond_broadcast(&destroyme->q_not_empty);
    if(fail != 0){
        printf("Broadcast failed.\n");
        return;
    }
    fail = pthread_mutex_unlock(&destroyme->qlock);
    if(fail != 0){
        printf("Unlock failed.\n");
        return;
    }   
    for(int i = 0; i < destroyme->num_threads; i++){
        fail = pthread_join(destroyme->threads[i], NULL);
        if(fail != 0){
            printf("Join failed.\n");
            return;
        }
    }
    
    fail = pthread_mutex_destroy(&destroyme->qlock);
    if(fail != 0){
        printf("Mutex destroy failed.\n");
        return;
    }
    fail = pthread_cond_destroy(&destroyme->q_empty);
    if(fail != 0){
        printf("Cond destroy failed.\n");
        return;
    }
    fail = pthread_cond_destroy(&destroyme->q_not_empty);
    if(fail != 0){
        printf("Cond destroy failed.\n");
        return;
    }
    free(destroyme->threads);
    free(destroyme);
}