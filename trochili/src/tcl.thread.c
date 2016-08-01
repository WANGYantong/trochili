/*************************************************************************************************
 *                                     Trochili RTOS Kernel                                      *
 *                                  Copyright(C) 2016 LIUXUMING                                  *
 *                                       www.trochili.com                                        *
 *************************************************************************************************/
#include <string.h>

#include "tcl.types.h"
#include "tcl.config.h"
#include "tcl.object.h"
#include "tcl.cpu.h"
#include "tcl.ipc.h"
#include "tcl.debug.h"
#include "tcl.kernel.h"
#include "tcl.timer.h"
#include "tcl.thread.h"

/* �ں˽��������ж���,���ھ��������е��̶߳�������������� */
static TThreadQueue ThreadReadyQueue;

/* �ں��̸߳������ж��壬������ʱ���������ߵ��̶߳�������������� */
static TThreadQueue ThreadAuxiliaryQueue;


/*************************************************************************************************
 *  ���ܣ����̴߳�ָ����״̬ת��������̬��ʹ���߳��ܹ������ں˵���                               *
 *  ������(1) pThread   �߳̽ṹ��ַ                                                             *
 *        (2) status    �̵߳�ǰ״̬�����ڼ��                                                   *
 *        (3) pError    ����������                                                             *
 *  ���أ�(1) eFailure                                                                           *
 *        (2) eSuccess                                                                           *
 *  ˵������������ǰ׺'u'(communal)��ʾ��������ȫ�ֺ���                                          *
 *************************************************************************************************/
static TState SetThreadReady(TThread* pThread, TThreadStatus status, TBool* pHiRP, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_STATUS;

    /* �߳�״̬У��,ֻ��״̬���ϵ��̲߳��ܱ����� */
    if (pThread->Status == status)
    {
        /*
         * �����̣߳�����̶߳��к�״̬ת��,ע��ֻ���жϴ���ʱ��
         * ��ǰ�̲߳Żᴦ���ں��̸߳���������(��Ϊ��û���ü��߳��л�)
         * ��ǰ�̷߳��ؾ�������ʱ��һ��Ҫ�ص���Ӧ�Ķ���ͷ
         * ���߳̽�����������ʱ������Ҫ�����̵߳�ʱ�ӽ�����
         */
        uThreadLeaveQueue(&ThreadAuxiliaryQueue, pThread);
        if (pThread == uKernelVariable.CurrentThread)
        {
            uThreadEnterQueue(&ThreadReadyQueue, pThread, eQuePosHead);
            pThread->Status = eThreadRunning;
        }
        else
        {
            uThreadEnterQueue(&ThreadReadyQueue, pThread, eQuePosTail);
            pThread->Status = eThreadReady;
        }
        state = eSuccess;
        error = THREAD_ERR_NONE;

        /* ��Ϊ�����̻߳����£����Դ�ʱpThreadһ�����ǵ�ǰ�߳� */
        if (pThread->Priority < uKernelVariable.CurrentThread->Priority)
        {
            *pHiRP = eTrue;
        }

#if (TCLC_TIMER_ENABLE)
        /* �����ȡ����ʱ��������Ҫֹͣ�̶߳�ʱ�� */
        if ((state == eSuccess) && (status == eThreadDelayed))
        {
            uTimerStop(&(pThread->Timer));
        }
#endif
    }

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  ���ܣ��̹߳�����                                                                           *
 *  ������(1) pThread   �߳̽ṹ��ַ                                                             *
 *        (2) status    �̵߳�ǰ״̬�����ڼ��                                                   *
 *        (3) ticks     �߳���ʱʱ��                                                             *
 *        (4) pError    ����������                                                             *
 *  ���أ�(1) eFailure                                                                           *
 *        (2) eSuccess                                                                           *
 *  ˵����                                                                                       *
 *************************************************************************************************/
static TState SetThreadUnready(TThread* pThread, TThreadStatus status, TTimeTick ticks, TBool* pHiRP, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_STATUS;

    /* ����������ǵ�ǰ�̣߳�����Ҫ���ȼ���ں��Ƿ��������� */
    if (pThread->Status == eThreadRunning)
    {
        /* ����ں˴�ʱ��ֹ�̵߳��ȣ���ô��ǰ�̲߳��ܱ����� */
        if (uKernelVariable.Schedulable == eTrue)
        {
            uThreadLeaveQueue(&ThreadReadyQueue, pThread);
            uThreadEnterQueue(&ThreadAuxiliaryQueue, pThread, eQuePosTail);
            pThread->Status = status;

            *pHiRP = eTrue;

            error = THREAD_ERR_NONE;
            state = eSuccess;
        }
        else
        {
            error = THREAD_ERR_FAULT;
        }
    }
    else if (pThread->Status == eThreadReady)
    {
        /* ������������̲߳��ǵ�ǰ�̣߳��򲻻������̵߳��ȣ�����ֱ�Ӵ����̺߳Ͷ��� */
        uThreadLeaveQueue(&ThreadReadyQueue, pThread);
        uThreadEnterQueue(&ThreadAuxiliaryQueue, pThread, eQuePosTail);
        pThread->Status = status;

        error = THREAD_ERR_NONE;
        state = eSuccess;
    }
    else
    {
        error = error;
    }

#if (TCLC_TIMER_ENABLE)
    if ((state == eSuccess) && (status == eThreadDelayed))
    {
        /* ���ò������̶߳�ʱ�� */
        uTimerConfig(&(pThread->Timer), eThreadTimer, ticks);
        uTimerStart(&(pThread->Timer), 0U);
    }
#endif

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  ���ܣ���������̶߳����е�������ȼ�����                                                     *
 *  ��������                                                                                     *
 *  ���أ�HiRP (Highest Ready Priority)                                                          *
 *  ˵����                                                                                       *
 *************************************************************************************************/
static void CalcThreadHiRP(TPriority* priority)
{
    /* ����������ȼ���������˵���ں˷����������� */
    if (ThreadReadyQueue.PriorityMask == (TBitMask)0)
    {
        uDebugPanic("", __FILE__, __FUNCTION__, __LINE__);
    }
    *priority = CpuCalcHiPRIO(ThreadReadyQueue.PriorityMask);
}


#if (TCLC_THREAD_STACK_CHECK_ENABLE)
/*************************************************************************************************
 *  ���ܣ��澯�ͼ���߳�ջ�������                                                               *
 *  ������(1) pThread  �̵߳�ַ                                                                  *
 *  ���أ���                                                                                     *
 *  ˵����                                                                                       *
 *************************************************************************************************/
static void CheckThreadStack(TThread* pThread)
{
    if ((pThread->StackTop < pThread->StackBarrier) ||
            (*(TBase32*)(pThread->StackBarrier) != TCLC_THREAD_STACK_BARRIER_VALUE))
    {
        uKernelVariable.Diagnosis |= KERNEL_DIAG_THREAD_ERROR;
        pThread->Diagnosis |= THREAD_DIAG_STACK_OVERFLOW;
        uDebugPanic("", __FILE__, __FUNCTION__, __LINE__);
    }

    if (pThread->StackTop < pThread->StackAlarm)
    {
        pThread->Diagnosis |= THREAD_DIAG_STACK_ALARM;
    }
}

#endif


/*************************************************************************************************
 *  ���ܣ��߳����м����������̵߳����ж�����Ϊ����                                               *
 *  ������(1) pThread  �̵߳�ַ                                                                  *
 *  ���أ���                                                                                     *
 *  ˵������������ǰ׺'x'(eXtreme)��ʾ��������Ҫ�����ٽ�������                                   *
 *************************************************************************************************/
static void xSuperviseThread(TThread* pThread)
{
    TReg32 imask;
    KNL_ASSERT((pThread == uKernelVariable.CurrentThread), "");

#if (TCLC_IRQ_ENABLE)
    if (pThread->Property &THREAD_PROP_ASR)
    {
        while (eTrue)
        {
            /*
            * �����û�ASR�߳������������ຯ�����ص��ǻ���ִ�к�
            * �ᱻϵͳ�Զ����𣬵ȴ��´λ��ѡ�
            */
            pThread->Entry(pThread->Argument);

            /*
            * ��ASR֮�н�����׼������ʱ����ʱISR���ܻ���һ�ν��볢�Ի���ASR��
            * ���ɴ�ʱASRΪ����״̬��Ϊ�˱���ASR��ʧ���ֵĻ��������������Ծ���
            * �������˼��
            */
            CpuEnterCritical(&imask);
            if (pThread->SyncValue == 0U)
            {
                uThreadSuspendSelf();
            }
            else
            {
                pThread->SyncValue = 0U;
            }
            CpuLeaveCritical(imask);
        }
    }
#endif

    /* ��ͨ�߳���Ҫע���û���С���˳����·Ƿ�ָ������������� */
    pThread->Entry(pThread->Argument);
    uKernelVariable.Diagnosis |= KERNEL_DIAG_THREAD_ERROR;
    pThread->Diagnosis |= THREAD_DIAG_INVALID_EXIT;
    uDebugPanic("", __FILE__, __FUNCTION__, __LINE__);
}



/*************************************************************************************************
 *  ���ܣ���ʼ���ں��̹߳���ģ��                                                                 *
 *  ��������                                                                                     *
 *  ���أ���                                                                                     *
 *  ˵�����ں��е��̶߳�����Ҫ��һ�¼��֣�                                                       *
 *        (1) �߳̾�������,���ڴ洢���о����߳�(�������е��߳�)���ں���ֻ��һ����������          *
 *        (2) �̸߳�������, ���г�ʼ��״̬����ʱ״̬������״̬���̶߳��洢����������С�         *
 *            ͬ���ں���ֻ��һ�����߶���                                                         *
 *        (3) IPC������߳���������                                                              *
 *************************************************************************************************/
void uThreadModuleInit(void)
{
    /* ����ں��Ƿ��ڳ�ʼ״̬ */
    if (uKernelVariable.State != eOriginState)
    {
        uDebugPanic("", __FILE__, __FUNCTION__, __LINE__);
    }

    memset(&ThreadReadyQueue, 0, sizeof(ThreadReadyQueue));
    memset(&ThreadAuxiliaryQueue, 0, sizeof(ThreadAuxiliaryQueue));

    uKernelVariable.ThreadReadyQueue = &ThreadReadyQueue;
    uKernelVariable.ThreadAuxiliaryQueue = &ThreadAuxiliaryQueue;
}

/* RULE
 * 1 ��ǰ�߳��뿪�������к��ٴμ����������ʱ��
 *   �����Ȼ�ǵ�ǰ�߳���һ��������Ӧ�Ķ���ͷ�������Ҳ����¼���ʱ��Ƭ��
 *   ����Ѿ����ǵ�ǰ�߳���һ��������Ӧ�Ķ���β�������Ҳ����¼���ʱ��Ƭ��
 * 2 ��ǰ�߳��ھ��������ڲ��������ȼ�ʱ�����µĶ�����Ҳһ��Ҫ�ڶ���ͷ��
 */

/*************************************************************************************************
 *  ���ܣ����̼߳��뵽ָ�����̶߳�����                                                           *
 *  ������(1) pQueue  �̶߳��е�ַ��ַ                                                           *
 *        (2) pThread �߳̽ṹ��ַ                                                               *
 *        (3) pos     �߳����̶߳����е�λ��                                                     *
 *  ���أ���                                                                                     *
 *  ˵����                                                                                       *
 *************************************************************************************************/
void uThreadEnterQueue(TThreadQueue* pQueue, TThread* pThread, TQueuePos pos)
{
    TPriority priority;
    TObjNode** pHandle;

    /* ����̺߳��̶߳��� */
    KNL_ASSERT((pThread != (TThread*)0), "");
    KNL_ASSERT((pThread->Queue == (TThreadQueue*)0), "");

    /* �����߳����ȼ��ó��߳�ʵ�������ֶ��� */
    priority = pThread->Priority;
    pHandle = &(pQueue->Handle[priority]);

    /* ���̼߳���ָ���ķֶ��� */
    uObjQueueAddFifoNode(pHandle, &(pThread->ObjNode), pos);

    /* �����߳��������� */
    pThread->Queue = pQueue;

    /* �趨���߳����ȼ�Ϊ�������ȼ� */
    pQueue->PriorityMask |= (0x1 << priority);
}


/*************************************************************************************************
 *  ���ܣ����̴߳�ָ�����̶߳������Ƴ�                                                           *
 *  ������(1) pQueue  �̶߳��е�ַ��ַ                                                           *
 *        (2) pThread �߳̽ṹ��ַ                                                               *
 *  ���أ���                                                                                     *
 *  ˵����FIFO PRIO���ַ�����Դ�ķ�ʽ                                                            *
 *************************************************************************************************/
void uThreadLeaveQueue(TThreadQueue* pQueue, TThread* pThread)
{
    TPriority priority;
    TObjNode** pHandle;

    /* ����߳��Ƿ����ڱ�����,������������ں˷����������� */
    KNL_ASSERT((pThread != (TThread*)0), "");
    KNL_ASSERT((pQueue == pThread->Queue), "");

    /* �����߳����ȼ��ó��߳�ʵ�������ֶ��� */
    priority = pThread->Priority;
    pHandle = &(pQueue->Handle[priority]);

    /* ���̴߳�ָ���ķֶ�����ȡ�� */
    uObjQueueRemoveNode(pHandle, &(pThread->ObjNode));

    /* �����߳��������� */
    pThread->Queue = (TThreadQueue*)0;

    /* �����߳��뿪���к�Զ������ȼ�������ǵ�Ӱ�� */
    if (pQueue->Handle[priority] == (TObjNode*)0)
    {
        /* �趨���߳����ȼ�δ���� */
        pQueue->PriorityMask &= (~(0x1 << priority));
    }
}


/*************************************************************************************************
 *  ���ܣ��߳�ʱ��Ƭ������������ʱ��Ƭ�жϴ���ISR�л���ñ�����                                  *
 *  ��������                                                                                     *
 *  ���أ���                                                                                     *
 *  ˵��������������˵�ǰ�̵߳�ʱ��Ƭ����������û��ѡ����Ҫ���ȵĺ���̺߳ͽ����߳��л�         *
 *************************************************************************************************/
/*
 * ��ǰ�߳̿��ܴ���3��λ��
 * 1 �������е�ͷλ��(�κ����ȼ�)
 * 2 �������е�����λ��(�κ����ȼ�)
 * 3 ����������
 * ֻ�����1����Ҫ����ʱ��Ƭ��ת�Ĵ���������ʱ���漰�߳��л�,��Ϊ������ֻ��ISR�е��á�
 */

/* ������Ҫ����Ӧ�ô�������ж����ȼ���� */
void uThreadTickISR(void)
{
    TThread* pThread;
    TObjNode* pHandle;
    TPriority priority;

    /* ����ǰ�߳�ʱ��Ƭ��ȥ1��������,�߳������ܽ�������1 */
    pThread = uKernelVariable.CurrentThread;
    pThread->Ticks--;
    pThread->Jiffies++;

    /* �������ʱ��Ƭ������� */
    if (pThread->Ticks == 0U)
    {
        /* �ָ��̵߳�ʱ�ӽ����� */
        pThread->Ticks = pThread->BaseTicks;

        /* ����ں˴�ʱ�����̵߳��� */
        if (uKernelVariable.Schedulable == eTrue)
        {
            /* �ж��߳��ǲ��Ǵ����ں˾����̶߳��е�ĳ�����ȼ��Ķ���ͷ */
            pHandle = ThreadReadyQueue.Handle[pThread->Priority];
            if ((TThread*)(pHandle->Owner) == pThread)
            {
                priority = pThread->Priority;
                /*
                 * ����ʱ��Ƭ���ȣ�֮��pThread�����̶߳���β��,
                 * ��ǰ�߳������̶߳���Ҳ����ֻ�е�ǰ�߳�Ψһ1���߳�
                 */
                ThreadReadyQueue.Handle[priority] = (ThreadReadyQueue.Handle[priority])->Next;

                /* ���߳�״̬��Ϊ����,׼���߳��л� */
                pThread->Status = eThreadReady;
            }
        }
    }
}


/*************************************************************************************************
 *  ���ܣ����������̵߳���                                                                       *
 *  ��������                                                                                     *
 *  ���أ���                                                                                     *
 *  ˵�����̵߳ĵ���������ܱ�ISR����ȡ��                                                        *
 *************************************************************************************************/
/*
 * 1 ��ǰ�߳��뿪���м������������������У��ٴν������ʱ,ʱ��Ƭ��Ҫ���¼���,
 *   �ڶ����е�λ��Ҳ�涨һ�����ڶ�β
 * 2 ���µ�ǰ�̲߳�����߾������ȼ���ԭ����
 *   1 ������ȼ����ߵ��߳̽����������
 *   2 ��ǰ�߳��Լ��뿪����
 *   3 ����̵߳����ȼ������
 *   4 ��ǰ�̵߳����ȼ�������
 *   5 ��ǰ�߳�Yiled
 *   6 ʱ��Ƭ�ж��У���ǰ�̱߳���ת
 * 3 ��cortex��������, ������һ�ֿ���:
 *   ��ǰ�߳��ͷ��˴�����������PendSV�жϵõ���Ӧ֮ǰ���������������ȼ��жϷ�����
 *   �ڸ߼�isr���ְѵ�ǰ�߳���Ϊ������
 *   1 ���ҵ�ǰ�߳���Ȼ����߾������ȼ���
 *   2 ���ҵ�ǰ�߳���Ȼ����߾����̶߳��еĶ���ͷ��
 *   ��ʱ��Ҫ����ȡ��PENDSV�Ĳ��������⵱ǰ�̺߳��Լ��л�
 */
void uThreadSchedule(void)
{
    TPriority priority;

    /* ����������ȼ���������˵���ں˷����������� */
    if (ThreadReadyQueue.PriorityMask == (TBitMask)0)
    {
        uDebugPanic("", __FILE__, __FUNCTION__, __LINE__);
    }

    /* ������߾������ȼ�����ú���̣߳��������߳�ָ��Ϊ����˵���ں˷����������� */
    CalcThreadHiRP(&priority);
    uKernelVariable.NomineeThread = (TThread*)((ThreadReadyQueue.Handle[priority])->Owner);
    if (uKernelVariable.NomineeThread == (TThread*)0)
    {
        uDebugPanic("", __FILE__, __FUNCTION__, __LINE__);
    }

    /*
     * ����̵߳����ȼ���ռ����ʱ��Ƭ��ת;
     * ��������̵߳���(��������������"��ռ"��"����"�ĺ���)
     */
    if (uKernelVariable.NomineeThread != uKernelVariable.CurrentThread)
    {
#if (TCLC_THREAD_STACK_CHECK_ENABLE)
        CheckThreadStack(uKernelVariable.NomineeThread);
#endif
        uKernelVariable.NomineeThread->Status = eThreadRunning;
        if (uKernelVariable.CurrentThread->Status == eThreadRunning)
        {
            uKernelVariable.CurrentThread->Status = eThreadReady;
        }
        CpuConfirmThreadSwitch();
    }
    else
    {
        CpuCancelThreadSwitch();
        uKernelVariable.CurrentThread->Status = eThreadRunning;
    }
}


/*************************************************************************************************
 *  ���ܣ��߳̽ṹ��ʼ������                                                                     *
 *  ������(1)  pThread  �߳̽ṹ��ַ                                                             *
 *        (2)  status   �̵߳ĳ�ʼ״̬                                                           *
 *        (3)  property �߳�����                                                                 *
 *        (4)  acapi    ���̹߳���API�����ɿ���                                                  *
 *        (5)  pEntry   �̺߳�����ַ                                                             *
 *        (6)  TArgument�̺߳�������                                                             *
 *        (7)  pStack   �߳�ջ��ַ                                                               *
 *        (8)  bytes    �߳�ջ��С������Ϊ��λ                                                   *
 *        (9)  priority �߳����ȼ�                                                               *
 *        (10) ticks    �߳�ʱ��Ƭ����                                                           *
 *  ���أ�(1)  eFailure                                                                          *
 *        (2)  eSuccess                                                                          *
 *  ˵����ע��ջ��ʼ��ַ��ջ��С��ջ�澯��ַ���ֽڶ�������                                       *
 *  ˵������������ǰ׺'u'(Universal)��ʾ������Ϊģ���ͨ�ú���                                   *
 *************************************************************************************************/
void uThreadCreate(TThread* pThread, TThreadStatus status, TProperty property, TBitMask acapi,
                   TThreadEntry pEntry, TArgument argument, void* pStack, TBase32 bytes,
                   TPriority priority, TTimeTick ticks)
{
    TThreadQueue* pQueue;

    /* �����߳�ջ������ݺ͹����̳߳�ʼջջ֡ */
    KNL_ASSERT((bytes >= TCLC_CPU_MINIMAL_STACK), "");

    /* ջ��С����4byte���� */
    bytes &= (~((TBase32)0x3));
    pThread->StackBase = (TBase32)pStack + bytes;

    /* ����߳�ջ�ռ� */
    if (property &THREAD_PROP_CLEAN_STACK)
    {
        memset(pStack, 0U, bytes);
    }

    /* ����(α��)�̳߳�ʼջ֡,���ｫ�߳̽ṹ��ַ��Ϊ��������xSuperviseThread()���� */
    CpuBuildThreadStack(&(pThread->StackTop), pStack, bytes, (void*)(&xSuperviseThread),
                        (TArgument)pThread);

    /* �����߳�ջ�澯��ַ */
#if (TCLC_THREAD_STACK_CHECK_ENABLE)
    pThread->StackAlarm = (TBase32)pStack + bytes - (bytes* TCLC_THREAD_STACK_ALARM_RATIO) / 100;
    pThread->StackBarrier = (TBase32)pStack;
    (*(TAddr32*)pStack) = TCLC_THREAD_STACK_BARRIER_VALUE;
#endif

    /* �����߳�ʱ��Ƭ��ز��� */
    pThread->Ticks = ticks;
    pThread->BaseTicks = ticks;
    pThread->Jiffies = 0U;

    /* �����߳����ȼ� */
    pThread->Priority = priority;
    pThread->BasePriority = priority;

    /* �����߳�ΨһID��ֵ */
    pThread->ThreadID = uKernelVariable.ObjID;
    uKernelVariable.ObjID++;

    /* �����߳���ں������̲߳��� */
    pThread->Entry = pEntry;
    pThread->Argument = argument;

    /* �����߳�����������Ϣ */
    pThread->Queue = (TThreadQueue*)0;

    /* �����̶߳�ʱ����Ϣ */
#if (TCLC_TIMER_ENABLE)
    uTimerCreate(&(pThread->Timer), (TProperty)0, eThreadTimer, TCLM_MAX_VALUE64,
                 (TTimerRoutine)0, (TArgument)0, (void*)pThread);
#endif

    /* ����߳�IPC���������� */
#if (TCLC_IPC_ENABLE)
    uIpcInitContext(&(pThread->IpcContext), (void*)pThread);
#endif

    /* ����߳�ռ�е���(MUTEX)���� */
#if ((TCLC_IPC_ENABLE) && (TCLC_IPC_MUTEX_ENABLE))
    pThread->LockList = (TObjNode*)0;
#endif

#if (TCLC_IRQ_ENABLE)
    pThread->SyncValue = 0;
#endif

    /* ��ʼ�߳����������Ϣ */
    pThread->Diagnosis = THREAD_DIAG_NORMAL;

    /* �����߳��ܹ�֧�ֵ��̹߳���API */
    pThread->ACAPI = acapi;

    /* �����߳������ڵ���Ϣ���̴߳�ʱ�������κ��̶߳��� */
    pThread->ObjNode.Owner = (void*)pThread;
    pThread->ObjNode.Data = (TBase32*)(&(pThread->Priority));
    pThread->ObjNode.Prev = (TObjNode*)0;
    pThread->ObjNode.Next = (TObjNode*)0;
    pThread->ObjNode.Handle = (TObjNode**)0;

    /* ���̼߳����ں��̶߳��У������߳�״̬ */
    pQueue = (status == eThreadReady) ? (&ThreadReadyQueue): (&ThreadAuxiliaryQueue);
    uThreadEnterQueue(pQueue, pThread, eQuePosTail);
    pThread->Status = status;

    /* ����߳��Ѿ���ɳ�ʼ�� */
    property |= THREAD_PROP_READY;
    pThread->Property = property;
}


/*************************************************************************************************
 *  ���ܣ��߳�ע��                                                                               *
 *  ������(1) pThread �߳̽ṹ��ַ                                                               *
 *  ���أ�(1) eFailure                                                                           *
 *        (2) eSuccess                                                                           *
 *  ˵������ʼ���̺߳Ͷ�ʱ���̲߳��ܱ�ע��                                                       *
 *************************************************************************************************/
TState uThreadDelete(TThread* pThread, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_STATUS;

    if (pThread->Status == eThreadDormant)
    {
#if ((TCLC_IPC_ENABLE) && (TCLC_IPC_MUTEX_ENABLE))
        if (pThread->LockList)
        {
            error = THREAD_ERR_FAULT;
            state = eFailure;
        }
        else
#endif
        {
            uThreadLeaveQueue(pThread->Queue, pThread);
#if (TCLC_TIMER_ENABLE)
            uTimerDelete(&(pThread->Timer));
#endif
            memset(pThread, 0, sizeof(pThread));
            error = THREAD_ERR_NONE;
            state = eSuccess;
        }
    }
    *pError = error;
    return state;
}


/*************************************************************************************************
 *  ���ܣ������߳����ȼ�                                                                         *
 *  ������(1) pThread  �߳̽ṹ��ַ                                                              *
 *        (2) priority �߳����ȼ�                                                                *
 *        (3) flag     �Ƿ�SetPriority API����                                                 *
 *        (4) pError   ����������                                                              *
 *  ���أ�(1) eFailure �����߳����ȼ�ʧ��                                                        *
 *        (2) eSuccess �����߳����ȼ��ɹ�                                                        *
 *  ˵�����������ʱ�޸����ȼ������޸��߳̽ṹ�Ļ������ȼ�                                     *
 *************************************************************************************************/
TState uThreadSetPriority(TThread* pThread, TPriority priority, TBool flag, TBool* pHiRP, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_PRIORITY;
    TPriority newPrio;

    if (pThread->Priority != priority)
    {
        if (pThread->Status == eThreadBlocked)
        {
            uIpcSetPriority(&(pThread->IpcContext), priority);
            state = eSuccess;
            error = THREAD_ERR_NONE;
        }
        /*
         * �����̵߳������ȼ�ʱ������ֱ�ӵ������ھ����̶߳����еķֶ���
         * ���ڴ��ھ����̶߳����еĵ�ǰ�̣߳�����޸��������ȼ���
         * ��Ϊ��������Ƴ��߳̾������У����Լ�ʹ�ں˲���������Ҳû����
         */
        else if (pThread->Status == eThreadReady)
        {
            uThreadLeaveQueue(&ThreadReadyQueue, pThread);
            pThread->Priority = priority;
            uThreadEnterQueue(&ThreadReadyQueue, pThread, eQuePosTail);

            /*
             * �õ���ǰ�������е���߾������ȼ�����Ϊ�����߳�(������ǰ�߳�)
             * ���߳̾��������ڵ����ڻᵼ�µ�ǰ�߳̿��ܲ���������ȼ���
             */
            if (priority < uKernelVariable.CurrentThread->Priority)
            {
                *pHiRP = eTrue;
            }
            state = eSuccess;
            error = THREAD_ERR_NONE;
        }
        else if (pThread->Status == eThreadRunning)
        {
            /*
             * ���赱ǰ�߳����ȼ������Ψһ����������������ȼ�֮����Ȼ����ߣ�
             * �������µ����ȼ����ж�������̣߳���ô��ðѵ�ǰ�̷߳����µľ�������
             * ��ͷ������������������ʽ��ʱ��Ƭ��ת����ǰ�߳��Ⱥ󱻶�ε������ȼ�ʱ��ֻ��
             * ÿ�ζ��������ڶ���ͷ���ܱ�֤�����һ�ε������ȼ��󻹴��ڶ���ͷ��
             */
            uThreadLeaveQueue(&ThreadReadyQueue, pThread);
            pThread->Priority = priority;
            uThreadEnterQueue(&ThreadReadyQueue, pThread, eQuePosHead);

            /*
             * ��Ϊ��ǰ�߳����߳̾��������ڵ����ڻᵼ�µ�ǰ�߳̿��ܲ���������ȼ���
             * ������Ҫ���¼��㵱ǰ�������е���߾������ȼ���
             */
            CalcThreadHiRP(&newPrio);
            if (newPrio < uKernelVariable.CurrentThread->Priority)
            {
                *pHiRP = eTrue;
            }

            state = eSuccess;
            error = THREAD_ERR_NONE;
        }
        else
        {
            /*����״̬���̶߳��ڸ������������ֱ���޸����ȼ�*/
            pThread->Priority = priority;
            state = eSuccess;
            error = THREAD_ERR_NONE;
        }

        /* �����Ҫ���޸��̶̹߳����ȼ� */
        if (flag == eTrue)
        {
            pThread->BasePriority = priority;
        }
    }

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  ���ܣ����̴߳ӹ���״̬ת��������̬��ʹ���߳��ܹ������ں˵���                                 *
 *  ������(1) pThread   �߳̽ṹ��ַ                                                             *
 *  ���أ�(1) eFailure                                                                           *
 *        (2) eSuccess                                                                           *
 *  ˵������������ǰ׺'u'(communal)��ʾ��������ȫ�ֺ���                                          *
 *************************************************************************************************/
void uThreadResumeFromISR(TThread* pThread)
{
    /*
     * �����̣߳�����̶߳��к�״̬ת��,ע��ֻ���жϴ���ʱ��
     * ��ǰ�̲߳Żᴦ���ں��̸߳���������(��Ϊ��û���ü��߳��л�)
     * ��ǰ�̷߳��ؾ�������ʱ��һ��Ҫ�ص���Ӧ�Ķ���ͷ
     * ���߳̽�����������ʱ������Ҫ�����̵߳�ʱ�ӽ�����
     */
    if (pThread->Status == eThreadSuspended)
    {
        uThreadLeaveQueue(&ThreadAuxiliaryQueue, pThread);
        if (pThread == uKernelVariable.CurrentThread)
        {
            uThreadEnterQueue(&ThreadReadyQueue, pThread, eQuePosHead);
            pThread->Status = eThreadRunning;
        }
        else
        {
            uThreadEnterQueue(&ThreadReadyQueue, pThread, eQuePosTail);
            pThread->Status = eThreadReady;
        }
    }
#if (TCLC_IRQ_ENABLE)
    else
    {
        if (pThread->Property &THREAD_PROP_ASR)
        {
            pThread->SyncValue = 1U;
        }
    }
#endif
}


void uThreadSuspendSelf(void)
{
    /* ����Ŀ���ǵ�ǰ�߳� */
    TThread* pThread = uKernelVariable.CurrentThread;

    /* ����ǰ�̹߳�������ں˴�ʱ��ֹ�̵߳��ȣ���ô��ǰ�̲߳��ܱ����� */
    if (uKernelVariable.Schedulable == eTrue)
    {
        uThreadLeaveQueue(&ThreadReadyQueue, pThread);
        uThreadEnterQueue(&ThreadAuxiliaryQueue, pThread, eQuePosTail);
        pThread->Status = eThreadSuspended;
        uThreadSchedule();
    }
    else
    {
        uKernelVariable.Diagnosis |= KERNEL_DIAG_SCHED_ERROR;
        pThread->Diagnosis |= THREAD_DIAG_NORMAL;
        uDebugPanic("", __FILE__, __FUNCTION__, __LINE__);
    }
}


/*************************************************************************************************
 *  ���ܣ��߳̽ṹ��ʼ������                                                                     *
 *  ������(1)  pThread  �߳̽ṹ��ַ                                                             *
 *        (2)  status   �̵߳ĳ�ʼ״̬                                                           *
 *        (3)  property �߳�����                                                                 *
 *        (4)  acapi    ���̹߳���API�����ɿ���                                                  *
 *        (5)  pEntry   �̺߳�����ַ                                                             *
 *        (6)  pArg     �̺߳�������                                                             *
 *        (7)  pStack   �߳�ջ��ַ                                                               *
 *        (8)  bytes    �߳�ջ��С������Ϊ��λ                                                   *
 *        (9)  priority �߳����ȼ�                                                               *
 *        (10) ticks    �߳�ʱ��Ƭ����                                                           *
 *        (11) pError   ��ϸ���ý��                                                             *
 *  ���أ�(1)  eFailure                                                                          *
 *        (2)  eSuccess                                                                          *
 *  ˵������������ǰ׺'x'(eXtreme)��ʾ��������Ҫ�����ٽ�������                                   *
 *************************************************************************************************/
TState xThreadCreate(TThread* pThread, TThreadStatus status, TProperty property, TBitMask acapi,
                     TThreadEntry pEntry, TArgument argument, void* pStack, TBase32 bytes,
                     TPriority priority, TTimeTick ticks, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_FAULT;
    TReg32 imask;

    CpuEnterCritical(&imask);

    /* ֻ�������̴߳�������ñ����� */
    if (uKernelVariable.State == eThreadState)
    {
        /* ����߳��Ƿ��Ѿ�����ʼ�� */
        if (!(pThread->Property &THREAD_PROP_READY))
        {
            uThreadCreate(pThread, status, property, acapi, pEntry, argument, pStack, bytes,
                          priority, ticks);
            error = THREAD_ERR_NONE;
            state = eSuccess;
        }
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  ���ܣ��߳�ע��                                                                               *
 *  ������(1) pThread �߳̽ṹ��ַ                                                               *
 *        (2) pError  ��ϸ���ý��                                                               *
 *  ���أ�(1) eFailure                                                                           *
 *        (2) eSuccess                                                                           *
 *  ˵����IDLE�̡߳��жϴ����̺߳Ͷ�ʱ���̲߳��ܱ�ע��                                           *
 *************************************************************************************************/
TState xThreadDelete(TThread* pThread, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_FAULT;
    TReg32 imask;

    CpuEnterCritical(&imask);

    /* ֻ�������̴߳�������ñ����� */
    if (uKernelVariable.State == eThreadState)
    {
        /* ���û�и������������̵߳�ַ����ǿ��ʹ�õ�ǰ�߳� */
        if (pThread == (TThread*)0)
        {
            pThread = uKernelVariable.CurrentThread;
        }

        /* ����߳��Ƿ��Ѿ�����ʼ�� */
        if (pThread->Property &THREAD_PROP_READY)
        {
            /* ����߳��Ƿ�������API���� */
            if (pThread->ACAPI &THREAD_ACAPI_DEINIT)
            {
                state = uThreadDelete(pThread, &error);
            }
            else
            {
                error = THREAD_ERR_ACAPI;
            }
        }
        else
        {
            error = THREAD_ERR_UNREADY;
        }
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  ���ܣ������߳����ȼ�                                                                         *
 *  ������(1) pThread  �߳̽ṹ��ַ                                                              *
 *        (2) priority �߳����ȼ�                                                                *
 *        (3) pError   ��ϸ���ý��                                                              *
 *  ���أ�(1) eFailure �����߳����ȼ�ʧ��                                                        *
 *        (2) eSuccess �����߳����ȼ��ɹ�                                                        *
 *  ˵����(1) �������ʱ�޸����ȼ������޸��߳̽ṹ�Ļ������ȼ�����                             *
 *        (2) ������ʵʩ���ȼ��̳�Э���ʱ����AUTHORITY����                                    *
 *************************************************************************************************/
TState xThreadSetPriority(TThread* pThread, TPriority priority, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_FAULT;
    TBool HiRP = eFalse;
    TReg32 imask;

    CpuEnterCritical(&imask);

    /* ֻ�������̴߳�������ñ����� */
    if (uKernelVariable.State == eThreadState)
    {
        /* ���û�и������������̵߳�ַ����ǿ��ʹ�õ�ǰ�߳� */
        if (pThread == (TThread*)0)
        {
            pThread = uKernelVariable.CurrentThread;
        }

        /* ����߳��Ƿ��Ѿ�����ʼ�� */
        if (pThread->Property &THREAD_PROP_READY)
        {
            /* ����߳��Ƿ�������API���� */
            if (pThread->ACAPI &THREAD_ACAPI_SET_PRIORITY)
            {
                if ((!(pThread->Property & THREAD_PROP_PRIORITY_FIXED)) &&
                        (pThread->Property & THREAD_PROP_PRIORITY_SAFE))
                {
                    state = uThreadSetPriority(pThread, priority, eTrue, &HiRP, &error);
                    if ((uKernelVariable.Schedulable == eTrue) && (HiRP == eTrue))
                    {
                        uThreadSchedule();
                    }
                    else
                    {
                        error = THREAD_ERR_FAULT;
                        state = eFailure;
                    }
                }
                else
                {
                    error = THREAD_ERR_ACAPI;
                }
            }
            else
            {
                error = THREAD_ERR_UNREADY;
            }
        }
    }
    CpuLeaveCritical(imask);

    *pError = error;
    return state;

}


/*************************************************************************************************
 *  ���ܣ��޸��߳�ʱ��Ƭ����                                                                     *
 *  ������(1) pThread �߳̽ṹ��ַ                                                               *
 *        (2) slice   �߳�ʱ��Ƭ����                                                             *
 *        (3) pError  ��ϸ���ý��                                                               *
 *  ���أ�(1) eSuccess                                                                           *
 *        (2) eFailure                                                                           *
 *  ˵����                                                                                       *
 *************************************************************************************************/
TState xThreadSetTimeSlice(TThread* pThread, TTimeTick ticks, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_FAULT;
    TReg32 imask;

    CpuEnterCritical(&imask);

    /* ֻ�������̴߳�������ñ����� */
    if (uKernelVariable.State == eThreadState)
    {
        /* ���û�и������������̵߳�ַ����ǿ��ʹ�õ�ǰ�߳� */
        if (pThread == (TThread*)0)
        {
            pThread = uKernelVariable.CurrentThread;
        }

        /* ����߳��Ƿ��Ѿ�����ʼ�� */
        if (pThread->Property &THREAD_PROP_READY)
        {
            /* ����߳��Ƿ�������API���� */
            if (pThread->ACAPI &THREAD_ACAPI_SET_SLICE)
            {
                /* �����߳�ʱ��Ƭ���� */
                if (pThread->BaseTicks > ticks)
                {
                    pThread->Ticks = (pThread->Ticks < ticks) ? (pThread->Ticks): ticks;
                }
                else
                {
                    pThread->Ticks += (ticks - pThread->BaseTicks);
                }
                pThread->BaseTicks = ticks;

                error = THREAD_ERR_NONE;
                state = eSuccess;
            }
            else
            {
                error = THREAD_ERR_ACAPI;
            }
        }
        else
        {
            error = THREAD_ERR_UNREADY;
        }
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}



/*************************************************************************************************
 *  ���ܣ��̼߳��̵߳��Ⱥ�������ǰ�߳������ó�������(���־���״̬)                               *
 *  ������(1) pError    ��ϸ���ý��                                                             *
 *  ���أ�(1) eFailure                                                                           *
 *        (2) eSuccess                                                                           *
 *  ˵������Ϊ�����ƻ���߾������ȼ�ռ�ô�������ԭ��                                           *
 *        ����Yield����ֻ����ӵ����߾������ȼ����߳�֮�����                                    *
 *************************************************************************************************/
TState xThreadYield(TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_FAULT;
    TReg32 imask;
    TPriority priority;
    TThread* pThread;

    CpuEnterCritical(&imask);

    /* ֻ�����̻߳�����ͬʱ�ں������̵߳��ȵ������²��ܵ��ñ����� */
    if ((uKernelVariable.State == eThreadState) &&
            (uKernelVariable.Schedulable == eTrue))
    {
        /* ����Ŀ���ǵ�ǰ�߳� */
        pThread = uKernelVariable.CurrentThread;
        priority = pThread->Priority;

        /* ����߳��Ƿ��Ѿ�����ʼ�� */
        if (pThread->Property &THREAD_PROP_READY)
        {
            /* ����߳��Ƿ�������API���� */
            if (pThread->ACAPI &THREAD_ACAPI_YIELD)
            {
                /*
                 * ������ǰ�߳����ڶ��е�ͷָ��
                 * ��ǰ�߳������̶߳���Ҳ����ֻ�е�ǰ�߳�Ψһ1���߳�
                 */
                ThreadReadyQueue.Handle[priority] = (ThreadReadyQueue.Handle[priority])->Next;
                pThread->Status = eThreadReady;

                uThreadSchedule();
                error = THREAD_ERR_NONE;
                state = eSuccess;
            }
            else
            {
                error = THREAD_ERR_ACAPI;
            }
        }
        else
        {
            error = THREAD_ERR_UNREADY;
        }
    }
    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  ���ܣ��߳���ֹ��ʹ���̲߳��ٲ����ں˵���                                                     *
 *  ������(1) pThread �߳̽ṹ��ַ                                                               *
 *        (2) pError  ��ϸ���ý��                                                               *
 *  ���أ�(1) eFailure                                                                           *
 *        (2) eSuccess                                                                           *
 *  ˵����(1) ��ʼ���̺߳Ͷ�ʱ���̲߳��ܱ�����                                                   *
 *************************************************************************************************/
TState xThreadDeactivate(TThread* pThread, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_FAULT;
    TBool HiRP = eFalse;
    TReg32 imask;

    CpuEnterCritical(&imask);

    /* ֻ�������̴߳�������ñ����� */
    if (uKernelVariable.State == eThreadState)
    {
        /* ���û�и������������̵߳�ַ����ǿ��ʹ�õ�ǰ�߳� */
        if (pThread == (TThread*)0)
        {
            pThread = uKernelVariable.CurrentThread;
        }

        /* ����߳��Ƿ��Ѿ�����ʼ�� */
        if (pThread->Property &THREAD_PROP_READY)
        {
            /* ����߳��Ƿ�������API���� */
            if (pThread->ACAPI &THREAD_ACAPI_DEACTIVATE)
            {
                state = SetThreadUnready(pThread, eThreadDormant, 0U, &HiRP, &error);
                if (HiRP == eTrue)
                {
                    uThreadSchedule();
                }
            }
            else
            {
                error = THREAD_ERR_ACAPI;
            }
        }
        else
        {
            error = THREAD_ERR_UNREADY;
        }
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  ���ܣ������̣߳�ʹ���߳��ܹ������ں˵���                                                     *
 *  ������(1) pThread  �߳̽ṹ��ַ                                                              *
 *        (2) pError   ��ϸ���ý��                                                              *
 *  ���أ�(1) eFailure                                                                           *
 *        (2) eSuccess                                                                           *
 *  ˵����                                                                                       *
 *************************************************************************************************/
TState xThreadActivate(TThread* pThread, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_FAULT;
    TBool HiRP = eFalse;
    TReg32 imask;

    CpuEnterCritical(&imask);

    /* ֻ�������̴߳�������ñ����� */
    if (uKernelVariable.State == eThreadState)
    {
        /* ����߳��Ƿ��Ѿ�����ʼ�� */
        if (pThread->Property &THREAD_PROP_READY)
        {
            /* ����߳��Ƿ�������API���� */
            if (pThread->ACAPI &THREAD_ACAPI_ACTIVATE)
            {
                state = SetThreadReady(pThread, eThreadDormant, &HiRP, &error);
                if ((uKernelVariable.Schedulable == eTrue) && (HiRP == eTrue))
                {
                    uThreadSchedule();
                }
            }
            else
            {
                error = THREAD_ERR_ACAPI;
            }
        }
        else
        {
            error = THREAD_ERR_UNREADY;
        }
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  ���ܣ��̹߳�����                                                                           *
 *  ������(1) pThread �߳̽ṹ��ַ                                                               *
 *        (2) pError  ��ϸ���ý��                                                               *
 *  ���أ�(1) eFailure                                                                           *
 *        (2) eSuccess                                                                           *
 *  ˵����(1) �ں˳�ʼ���̲߳��ܱ�����                                                           *
 *************************************************************************************************/
TState xThreadSuspend(TThread* pThread, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_FAULT;
    TBool HiRP = eFalse;
    TReg32 imask;

    CpuEnterCritical(&imask);

    /* ֻ�������̴߳�������ñ����� */
    if (uKernelVariable.State == eThreadState)
    {
        /* ���û�и������������̵߳�ַ����ǿ��ʹ�õ�ǰ�߳� */
        if (pThread == (TThread*)0)
        {
            pThread = uKernelVariable.CurrentThread;
        }

        /* ����߳��Ƿ��Ѿ�����ʼ�� */
        if (pThread->Property &THREAD_PROP_READY)
        {
            /* ����߳��Ƿ�������API���� */
            if (pThread->ACAPI &THREAD_ACAPI_SUSPEND)
            {
                state = SetThreadUnready(pThread, eThreadSuspended, 0U, &HiRP, &error);
                if (HiRP == eTrue)
                {
                    uThreadSchedule();
                }
            }
            else
            {
                error = THREAD_ERR_ACAPI;
            }
        }
        else
        {
            error = THREAD_ERR_UNREADY;
        }
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  ���ܣ��߳̽�Һ���                                                                           *
 *  ������(1) pThread �߳̽ṹ��ַ                                                               *
 *        (2) pError  ��ϸ���ý��                                                               *
 *  ���أ�(1) eFailure                                                                           *
 *        (2) eSuccess                                                                           *
 *  ˵����                                                                                       *
 *************************************************************************************************/
TState xThreadResume(TThread* pThread, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_FAULT;
    TBool HiRP = eFalse;
    TReg32 imask;

    CpuEnterCritical(&imask);

    /* ֻ�������̴߳�������ñ����� */
    if (uKernelVariable.State == eThreadState)
    {
        /* ����߳��Ƿ��Ѿ�����ʼ�� */
        if (pThread->Property &THREAD_PROP_READY)
        {
            /* ����߳��Ƿ�������API���� */
            if (pThread->ACAPI &THREAD_ACAPI_RESUME)
            {
                state = SetThreadReady(pThread, eThreadSuspended, &HiRP, &error);
                if ((uKernelVariable.Schedulable == eTrue) && (HiRP == eTrue))
                {
                    uThreadSchedule();
                }
            }
            else
            {
                error = THREAD_ERR_ACAPI;
            }
        }
        else
        {
            error = THREAD_ERR_UNREADY;
        }
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


#if (TCLC_TIMER_ENABLE)
/*************************************************************************************************
 *  ���ܣ��߳���ʱģ��ӿں���                                                                   *
 *  ������(1) pThread �߳̽ṹ��ַ                                                               *
 *        (2) ticks   ��Ҫ��ʱ�ĵδ���Ŀ                                                         *
 *        (3) pError  ��ϸ���ý��                                                               *
 *  ���أ�(1) eSuccess                                                                           *
 *        (2) eFailure                                                                           *
 *  ˵����                                                                                       *
 *************************************************************************************************/
TState xThreadDelay(TThread* pThread, TTimeTick ticks, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_FAULT;
    TBool HiRP = eFalse;
    TReg32 imask;

    CpuEnterCritical(&imask);

    /* ֻ�������̴߳�������ñ����� */
    if (uKernelVariable.State == eThreadState)
    {
        /* ���û�и������������̵߳�ַ����ǿ��ʹ�õ�ǰ�߳� */
        if (pThread == (TThread*)0)
        {
            pThread = uKernelVariable.CurrentThread;
        }

        /* ����߳��Ƿ��Ѿ�����ʼ�� */
        if (pThread->Property &THREAD_PROP_READY)
        {
            /* ����߳��Ƿ�������API���� */
            if (pThread->ACAPI &THREAD_ACAPI_DELAY)
            {
                state = SetThreadUnready(pThread, eThreadDelayed, ticks, &HiRP, &error);
                if (HiRP == eTrue)
                {
                    uThreadSchedule();
                }
            }
            else
            {
                error = THREAD_ERR_ACAPI;
            }
        }
        else
        {
            error = THREAD_ERR_UNREADY;
        }
    }
    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  ���ܣ��߳���ʱȡ������                                                                       *
 *  ������(1) pThread �߳̽ṹ��ַ                                                               *
 *        (2) pError  ��ϸ���ý��                                                               *
 *  ���أ�(1) eSuccess                                                                           *
 *        (2) eFailure                                                                           *
 *  ˵����(1) �����������ʱ�޵ȴ���ʽ������IPC�߳����������ϵ��߳���Ч                          *
 *************************************************************************************************/
TState xThreadUndelay(TThread* pThread, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_FAULT;
    TBool HiRP = eFalse;
    TReg32 imask;

    CpuEnterCritical(&imask);

    /* ֻ�������̴߳�������ñ����� */
    if (uKernelVariable.State == eThreadState)
    {
        /* ����߳��Ƿ��Ѿ�����ʼ�� */
        if (pThread->Property &THREAD_PROP_READY)
        {
            /* ����߳��Ƿ�������API���� */
            if (pThread->ACAPI &THREAD_ACAPI_UNDELAY)
            {
                state = SetThreadReady(pThread, eThreadDelayed, &HiRP, &error);
                if ((uKernelVariable.Schedulable == eTrue) && (HiRP == eTrue))
                {
                    uThreadSchedule();
                }
            }
            else
            {
                error = THREAD_ERR_ACAPI;
            }
        }
        else
        {
            error = THREAD_ERR_UNREADY;
        }
    }
    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}
#endif


/*************************************************************************************************
 *  ���ܣ��߳̽�Һ���                                                                           *
 *  ������(1) pThread �߳̽ṹ��ַ                                                               *
 *        (2) pError  ��ϸ���ý��                                                               *
 *  ���أ�(1) eFailure                                                                           *
 *        (2) eSuccess                                                                           *
 *  ˵����                                                                                       *
 *************************************************************************************************/
TState xThreadUnblock(TThread* pThread, TError* pError)
{
    TState state = eFailure;
    TError error = THREAD_ERR_UNREADY;
    TBool HiRP = eFalse;
    TReg32 imask;

    CpuEnterCritical(&imask);

    /* ����߳��Ƿ��Ѿ�����ʼ�� */
    if (pThread->Property &THREAD_PROP_READY)
    {
        /* ����߳��Ƿ�������API���� */
        if (pThread->ACAPI &THREAD_ACAPI_UNBLOCK)
        {
            if (pThread->Status == eThreadBlocked)
            {
                /*
                 * �����������ϵ�ָ�������߳��ͷ�
                 * ���̻߳����£������ǰ�̵߳����ȼ��Ѿ��������߳̾������е�������ȼ���
                 * �����ں˴�ʱ��û�йر��̵߳��ȣ���ô����Ҫ����һ���߳���ռ
                 */
                uIpcUnblockThread(&(pThread->IpcContext), eFailure, IPC_ERR_ABORT, &HiRP);
                if ((uKernelVariable.State == eThreadState) &&
                        (uKernelVariable.Schedulable == eTrue) &&
                        (HiRP == eTrue))
                {
                    uThreadSchedule();
                }
                error = THREAD_ERR_NONE;
                state = eSuccess;
            }
            else
            {
                error = THREAD_ERR_STATUS;
            }
        }
        else
        {
            error = THREAD_ERR_ACAPI;
        }
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}