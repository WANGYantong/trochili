/*************************************************************************************************
 *                                     Trochili RTOS Kernel                                      *
 *                                  Copyright(C) 2016 LIUXUMING                                  *
 *                                       www.trochili.com                                        *
 *************************************************************************************************/
#include <string.h>

#include "tcl.types.h"
#include "tcl.config.h"
#include "tcl.cpu.h"
#include "tcl.debug.h"
#include "tcl.thread.h"
#include "tcl.kernel.h"
#include "tcl.ipc.h"
#include "tcl.flags.h"

#if ((TCLC_IPC_ENABLE) && (TCLC_IPC_FLAGS_ENABLE))

/*************************************************************************************************
 *  ���ܣ����Խ����¼����                                                                       *
 *  ������(1) pFlags   �¼���ǵĵ�ַ                                                            *
 *        (2) pPattern ��Ҫ���յı�ǵ����                                                      *
 *        (3) option   �����¼���ǵĲ���                                                        *
 *        (4) pError   ��ϸ���ý��                                                              *
 *  ����: (1) eFailure ����ʧ��                                                                  *
 *        (2) eSuccess �����ɹ�                                                                  *
 *  ˵����                                                                                       *
 *************************************************************************************************/
static TState ReceiveFlags(TFlags* pFlags, TBitMask* pPattern, TOption option, TError* pError)
{
    TState state = eFailure;
	TError error = IPC_ERR_FLAGS;
    TBitMask match;
    TBitMask pattern;

    pattern = *pPattern;
    match = (pFlags->Value) & pattern;
    if (((option & IPC_OPT_AND) && (match == pattern)) ||
            ((option & IPC_OPT_OR) && (match != 0U)))
    {
        if (option & IPC_OPT_CONSUME)
        {
            pFlags->Value &= (~match);
        }

        *pPattern = match;

        error = IPC_ERR_NONE;
        state = eSuccess;
    }

	*pError = error;
    return state;
}


/*************************************************************************************************
 *  ���ܣ����Է����¼����                                                                       *
 *  ������(1) pFlags   �¼���ǵĵ�ַ                                                            *
 *        (2) pPattern ��Ҫ���͵ı�ǵ����                                                      *
 *        (3) pHiRP    �Ƿ��ں����л��ѹ������߳�                                                *
 *        (4) pError   ��ϸ���ý��                                                              *
 *  ����: (1) eFailure ����ʧ��                                                                  *
 *        (2) eSuccess �����ɹ�                                                                  *
 *  ˵����                                                                                       *
 *************************************************************************************************/
static TState SendFlags(TFlags* pFlags, TBitMask pattern, TBool* pHiRP, TError* pError)
{
    TState state = eError;
    TError error = IPC_ERR_FLAGS;
    TObjNode* pHead;
    TObjNode* pTail;
    TObjNode* pCurrent;
    TOption   option;
    TBitMask  mask;
    TBitMask* pTemp;
    TIpcContext* pContext;

    /* ����¼��Ƿ���Ҫ���� */
    mask = pFlags->Value | pattern;
    if (mask != pFlags->Value)
    {
        error = eSuccess;
        state = IPC_ERR_NONE;

        /* ���¼����͵��¼������ */
        pFlags->Value |= pattern;

        /* �¼�����Ƿ����߳��ڵȴ��¼��ķ��� */
		if (pFlags->Property & IPC_PROP_PRIMQ_AVAIL)
        {
            /* ��ʼ�����¼����������� */
            pHead = pFlags->Queue.PrimaryHandle;
            pTail = pFlags->Queue.PrimaryHandle->Prev;
            do
            {
                pCurrent = pHead;
                pHead = pHead->Next;

                /* ��õȴ��¼���ǵ��̺߳���ص��¼��ڵ� */
                pContext =  (TIpcContext*)(pCurrent->Owner);
                option = pContext->Option;
                pTemp = (TBitMask*)(pContext->Data.Addr1);

                /* �õ�����Ҫ����¼���� */
                mask = pFlags->Value & (*pTemp);
                if (((option & IPC_OPT_AND) && (mask == *pTemp)) ||
                        ((option & IPC_OPT_OR) && (mask != 0U)))
                {
                    *pTemp = mask;
                    uIpcUnblockThread(pContext, eSuccess, IPC_ERR_NONE, pHiRP);

                    /* ����ĳЩ�¼�������¼�ȫ�������Ĵ��������˳� */
                    if (option & IPC_OPT_CONSUME)
                    {
                        pFlags->Value &= (~mask);
                        if (pFlags->Value == 0U)
                        {
                            break;
                        }
                    }
                }
            }
            while(pCurrent != pTail);
        }
    }

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  ���ܣ��߳�/ISR�����¼����                                                                   *
 *  ������(1) pFlags   �¼���ǵĵ�ַ                                                            *
 *        (2) pPattern ��Ҫ���յı�ǵ����                                                      *
 *        (3) timeo    ʱ������ģʽ�·����¼���ǵ�ʱ�޳���                                      *
 *        (4) option   �����¼���ǵĲ���                                                        *
 *        (5) pError   ��ϸ���ý��                                                              *
 *  ����: (1) eFailure ����ʧ��                                                                  *
 *        (2) eSuccess �����ɹ�                                                                  *
 *  ˵����                                                                                       *
 *************************************************************************************************/
TState xFlagsReceive(TFlags* pFlags, TBitMask* pPattern, TOption option, TTimeTick timeo,
                     TError* pError)
{
    TState state = eFailure;
    TError error = IPC_ERR_UNREADY;
    TIpcContext* pContext;
    TReg32 imask;

    CpuEnterCritical(&imask);

    if (pFlags->Property & IPC_PROP_READY)
    {
        /*
         * ������жϳ�����ñ�������ֻ���Է�������ʽ����¼����,
         * ������ʱ�������̵߳������⡣
         * ���ж���,��ǰ�߳�δ������߾������ȼ��߳�,Ҳδ�ش����ں˾����̶߳��У�
         * �����ڴ˴��õ���HiRP������κ����塣
         */
        state = ReceiveFlags(pFlags, pPattern, option, &error);

        /* ���û����������Ҫ����������̵߳��ȴ������� */
        if (!(option & IPC_OPT_NO_SCHED))
        {
            /*
             * ��Ϊ�¼�����̶߳����в�������¼����Ͷ��У����Բ���Ҫ�ж��Ƿ������߳�Ҫ���ȣ�
             * ����Ҫ�����Ƿ���Ҫ���¼����ĵ�����
             */
            if ((uKernelVariable.State == eThreadState) &&
                    (uKernelVariable.Schedulable == eTrue))
            {
                /*
                 * �����ǰ�̲߳��ܵõ��¼������Ҳ��õ��ǵȴ���ʽ��
                 * ��ô��ǰ�̱߳����������¼���ǵĵȴ������У�����ǿ���̵߳���
                 */
                if (state == eFailure)
                {
                    if (option & IPC_OPT_WAIT)
                    {
                        /* �õ���ǰ�̵߳�IPC�����Ľṹ��ַ */
                        pContext = &(uKernelVariable.CurrentThread->IpcContext);

                        /* �����̹߳�����Ϣ */
                        uIpcSaveContext(pContext, (void*)pFlags, (TBase32)pPattern, sizeof(TBase32),
                                        option | IPC_OPT_FLAGS, &state, &error);

                        /* ��ǰ�߳������ڸ��¼���ǵ��������У�ʱ�޻������޵ȴ�����IPC_OPT_TIMED�������� */
                        uIpcBlockThread(pContext, &(pFlags->Queue), timeo);

                        /* ��ǰ�̱߳������������̵߳���ִ�� */
                        uThreadSchedule();

                        CpuLeaveCritical(imask);
                        /*
                         * ��Ϊ��ǰ�߳��Ѿ�������IPC������߳��������У����Դ�������Ҫִ�б���̡߳�
                         * ���������ٴδ������߳�ʱ���ӱ����������С�
                         */
                        CpuEnterCritical(&imask);

                        /* ����߳�IPC������Ϣ */
                        uIpcCleanContext(pContext);
                    }
                }
            }
        }
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  ���ܣ��߳�/ISR���¼���Ƿ����¼�                                                             *
 *  ������(1) pFlags   �¼���ǵĵ�ַ                                                            *
 *        (2) pPattern ��Ҫ���յı�ǵ����                                                      *
 *        (3) pError   ��ϸ���ý��                                                              *
 *  ����: (1) eFailure   ����ʧ��                                                                *
 *        (2) eSuccess   �����ɹ�                                                                *
 *  ˵������������������ǰ�߳�����,���Բ��������̻߳���ISR������                               *
 *************************************************************************************************/
TState xFlagsSend(TFlags* pFlags, TBitMask pattern, TError* pError)
{
    TState state = eFailure;
    TError error = IPC_ERR_UNREADY;
    TBool HiRP = eFalse;
    TReg32 imask;

    CpuEnterCritical(&imask);

    if (pFlags->Property & IPC_PROP_READY)
    {
        /*
        * ������жϳ�����ñ�������ֻ���Է�������ʽ�����¼�,
        * ������ʱ�������̵߳������⡣
        * ���ж���,��ǰ�߳�δ������߾������ȼ��߳�,Ҳδ�ش����ں˾����̶߳��У�
        * �����ڴ˴��õ���HiRP������κ����塣
        */
        state = SendFlags(pFlags, pattern, &HiRP, &error);
        /*
         * �����ISR��������ֱ�ӷ��ء�
         * ֻ�����̻߳����²��������̵߳��Ȳſɼ�������
         */
        if ((uKernelVariable.State == eThreadState) &&
                (uKernelVariable.Schedulable == eTrue))
        {
            /* �����ǰ�߳̽���˸������ȼ��̵߳���������е��ȡ�*/
            if (state == eSuccess)
            {
                if (HiRP == eTrue)
                {
                    uThreadSchedule();
                }
            }
        }
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  ���ܣ���ʼ���¼����                                                                         *
 *  ������(1) pFlags     �¼���ǵĵ�ַ                                                          *
 *        (2) property   �¼���ǵĳ�ʼ����                                                      *
 *        (3) pError     ����������ϸ����ֵ                                                      *
 *  ����: (1) eFailure   ����ʧ��                                                                *
 *        (2) eSuccess   �����ɹ�                                                                *
 *  ˵����                                                                                       *
 *************************************************************************************************/
TState xFlagsCreate(TFlags* pFlags, TProperty property, TError* pError)
{
    TState state = eFailure;
    TError error = IPC_ERR_FAULT;
    TReg32 imask;

    CpuEnterCritical(&imask);

    if (!(pFlags->Property & IPC_PROP_READY))
    {
        property |= IPC_PROP_READY;
        pFlags->Property = property;
        pFlags->Value = 0U;

        pFlags->Queue.PrimaryHandle   = (TObjNode*)0;
        pFlags->Queue.AuxiliaryHandle = (TObjNode*)0;
        pFlags->Queue.Property        = &(pFlags->Property);

        state = eSuccess;
        error = IPC_ERR_NONE;
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  ���ܣ�ȡ���¼���ǳ�ʼ��                                                                     *
 *  ������(1) pFlags   �¼���ǵĵ�ַ                                                            *
 *        (2) pError   ����������ϸ����ֵ                                                        *
 *  ����: (1) eFailure ����ʧ��                                                                  *
 *        (2) eSuccess �����ɹ�                                                                  *
 *  ˵����                                                                                       *
 *************************************************************************************************/
TState xFlagsDelete(TFlags* pFlags, TError* pError)
{
    TState state = eFailure;
    TError error = IPC_ERR_UNREADY;
    TReg32 imask;
    TBool HiRP = eFalse;

    CpuEnterCritical(&imask);

    if (pFlags->Property & IPC_PROP_READY)
    {
        /* �����������ϵ����еȴ��̶߳��ͷţ������̵߳ĵȴ��������IPC_ERR_DELETE  */
        uIpcUnblockAll(&(pFlags->Queue), eFailure, IPC_ERR_DELETE, (void**)0, &HiRP);

        /* ����¼���Ƕ����ȫ������ */
        memset(pFlags, 0U, sizeof(TFlags));

        /*
         * ���̻߳����£������ǰ�̵߳����ȼ��Ѿ��������߳̾������е�������ȼ���
         * �����ں˴�ʱ��û�йر��̵߳��ȣ���ô����Ҫ����һ���߳���ռ
         */
        if ((uKernelVariable.State == eThreadState) &&
                (uKernelVariable.Schedulable == eTrue) &&
                (HiRP == eTrue))
        {
            uThreadSchedule();
        }
        state = eSuccess;
        error = IPC_ERR_NONE;
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  ����: ����¼������������                                                                   *
 *  ������(1) pFlags   �¼���ǵĵ�ַ                                                            *
 *        (2) pError   ��ϸ���ý��                                                              *
 *  ����: (1) eFailure   ����ʧ��                                                                *
 *        (2) eSuccess   �����ɹ�                                                                *
 *  ˵����                                                                                       *
 *************************************************************************************************/
TState xFlagsReset(TFlags* pFlags, TError* pError)
{
    TState state = eFailure;
    TError error = IPC_ERR_UNREADY;
    TReg32 imask;
    TBool HiRP = eFalse;

    CpuEnterCritical(&imask);

    if (pFlags->Property & IPC_PROP_READY)
    {
        /* �����������ϵ����еȴ��̶߳��ͷţ������̵߳ĵȴ��������IPC_ERR_RESET */
        uIpcUnblockAll(&(pFlags->Queue), eFailure, IPC_ERR_RESET, (void**)0, &HiRP);

        pFlags->Property &= IPC_RESET_FLAG_PROP;
        pFlags->Value = 0U;

        /*
         * ���̻߳����£������ǰ�̵߳����ȼ��Ѿ��������߳̾������е�������ȼ���
         * �����ں˴�ʱ��û�йر��̵߳��ȣ���ô����Ҫ����һ���߳���ռ
         */
        if ((uKernelVariable.State == eThreadState) &&
                (uKernelVariable.Schedulable == eTrue) &&
                (HiRP == eTrue))
        {
            uThreadSchedule();
        }
        state = eSuccess;
        error = IPC_ERR_NONE;
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}


/*************************************************************************************************
 *  ���ܣ��¼������ֹ����,��ָ�����̴߳��¼���ǵ��߳�������������ֹ����������                  *
 *  ������(1) pFlags   �¼���ǽṹ��ַ                                                          *
 *        (2) option   ����ѡ��                                                                  *
 *        (3) pThread  �̵߳�ַ                                                                  *
 *        (4) pError   ��ϸ���ý��                                                              *
 *  ���أ�(1) eSuccess                                                                           *
 *        (2) eFailure                                                                           *
 *  ˵����                                                                                       *
 *************************************************************************************************/
TState xFlagsFlush(TFlags* pFlags, TError* pError)
{
    TState state = eFailure;
    TError error = IPC_ERR_UNREADY;
    TReg32 imask;
    TBool HiRP = eFalse;

    CpuEnterCritical(&imask);

    if (pFlags->Property & IPC_PROP_READY)
    {
        /* ���¼�������������ϵ����еȴ��̶߳��ͷţ������̵߳ĵȴ��������TCLE_IPC_FLUSH  */
        uIpcUnblockAll(&(pFlags->Queue), eFailure, IPC_ERR_FLUSH, (void**)0, &HiRP);

        /*
         * ���̻߳����£������ǰ�̵߳����ȼ��Ѿ��������߳̾������е�������ȼ���
         * �����ں˴�ʱ��û�йر��̵߳��ȣ���ô����Ҫ����һ���߳���ռ
         */
        if ((uKernelVariable.State == eThreadState) &&
                (uKernelVariable.Schedulable == eTrue) &&
                (HiRP == eTrue))
        {
            uThreadSchedule();
        }
        state = eSuccess;
        error = IPC_ERR_NONE;
    }

    CpuLeaveCritical(imask);

    *pError = error;
    return state;
}

#endif
