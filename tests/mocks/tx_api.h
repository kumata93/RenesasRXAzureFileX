/***********************************************************************************************************************
 * File Name    : tx_api.h (Mock / Stub for Host Unit Testing)
 * Description  : Minimal ThreadX API definitions required for standalone/host testing.
 ***********************************************************************************************************************/

#ifndef TX_API_H
#define TX_API_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VOID                            void
typedef char                            CHAR;
typedef char                            BOOL;
typedef unsigned char                   UCHAR;
typedef int                             INT;
typedef unsigned int                    UINT;
typedef long                            LONG;
typedef unsigned long                   ULONG;
typedef short                           SHORT;
typedef unsigned short                  USHORT;
typedef unsigned long long              ULONG64;

#define ALIGN_TYPE                      ULONG
#define TX_NULL                         NULL
#define TX_FALSE                        0
#define TX_TRUE                         1
#define TX_SUCCESS                      0
#define TX_WAIT_FOREVER                 0xFFFFFFFFUL
#define TX_NO_WAIT                      0
#define TX_NO_TIME_SLICE                0
#define TX_AUTO_START                   1
#define TX_NO_INHERIT                   0
#define TX_TIMER_TICKS_PER_SECOND       100

#define TX_MEMSET(d, v, s)              memset((d), (v), (s))
#define TX_MEMCPY(d, s, n)              memcpy((d), (s), (n))

#define TX_INTERRUPT_SAVE_AREA          int _tx_save_area = 0; (void)_tx_save_area;
#define TX_DISABLE                      do {} while (0);
#define TX_RESTORE                      do {} while (0);

#define TX_THREAD_GET_SYSTEM_STATE()    0

typedef struct TX_MUTEX_STRUCT
{
    ULONG tx_mutex_id;
} TX_MUTEX;

typedef struct TX_THREAD_STRUCT
{
    ULONG tx_thread_id;
} TX_THREAD;

static inline UINT tx_mutex_create(TX_MUTEX *mutex_ptr, const CHAR *name, UINT inherit)
{
    (void)name; (void)inherit;
    if (mutex_ptr) mutex_ptr->tx_mutex_id = 0x4D555458; /* 'MUTX' */
    return TX_SUCCESS;
}

static inline UINT tx_mutex_delete(TX_MUTEX *mutex_ptr)
{
    if (mutex_ptr) mutex_ptr->tx_mutex_id = 0;
    return TX_SUCCESS;
}

static inline UINT tx_mutex_get(TX_MUTEX *mutex_ptr, ULONG wait_option)
{
    (void)mutex_ptr; (void)wait_option;
    return TX_SUCCESS;
}

static inline UINT tx_mutex_put(TX_MUTEX *mutex_ptr)
{
    (void)mutex_ptr;
    return TX_SUCCESS;
}

static inline UINT tx_thread_create(TX_THREAD *thread_ptr, const CHAR *name,
                                    VOID (*entry)(ULONG), ULONG entry_input,
                                    VOID *stack_start, ULONG stack_size,
                                    UINT priority, UINT preempt_threshold,
                                    ULONG time_slice, UINT auto_start)
{
    (void)name; (void)entry; (void)entry_input; (void)stack_start;
    (void)stack_size; (void)priority; (void)preempt_threshold;
    (void)time_slice; (void)auto_start;
    if (thread_ptr) thread_ptr->tx_thread_id = 0x54485244;
    return TX_SUCCESS;
}

#ifdef __cplusplus
}
#endif

#endif /* TX_API_H */
