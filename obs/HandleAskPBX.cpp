#include "HandleAskPBX.h"
#include "AskProxyDlg.h"
#include "UntiTool.h"
#include "askproxydef.h"
#include "AgentOp.h"
#include "QueueStatic.h"
#include "./ResponseMsg.h"
#include <map>

CHandleAskPBX::CHandleAskPBX()
    :m_AskMsgRun(0, NULL, NULL, PBXTHREADNUM), m_hSerialEvent(1, NULL, NULL, 1), m_DispatchRun(0, NULL, NULL, 10)
{

    m_strEvtPBX = "";

    m_AskMsgStop = false;
    m_HandAskPBXGrp = -1;

    m_DispatchAskPBXID = 0;
    m_DispatchStop = false;
}

CHandleAskPBX::~CHandleAskPBX()
{

}

BOOL CHandleAskPBX::StartHandleAskPBX(CAskProxyDlg* pDlg)
{
    m_pDlg = pDlg;

    m_HandAskPBXGrp = ACE_Thread_Manager::instance()->spawn_n(
                          PBXTHREADNUM,
                          CHandleAskPBX::HandleAskPBX,
                          this,
                          (THR_NEW_LWP|THR_JOINABLE|THR_INHERIT_SCHED|THR_SUSPENDED),
                          ACE_DEFAULT_THREAD_PRIORITY,
                          -1,
                          0,
                          m_HandAskPBXID
                      );
    if(m_HandAskPBXGrp == -1)
    {
        m_pDlg->m_Log.Log("create ask pbx thread fail");
        m_AskMsgStop = true;
        return FALSE;
    }
    else
    {
        m_pDlg->m_Log.Log("create ask pbx thread succ");
    }
    ACE_Thread_Manager::instance()->spawn(CHandleAskPBX::DispatchAskPBX,
                                          this,
                                          (THR_NEW_LWP|THR_JOINABLE|THR_INHERIT_SCHED|THR_SUSPENDED),
                                          &m_DispatchAskPBXID
                                         );
    if(m_DispatchAskPBXID==0)
    {
        m_pDlg->m_Log.Log("create handle ask pbx thread fail");
        return FALSE;
    }
    else
    {
        m_pDlg->m_Log.Log("create handle ask pbx thread succ");
    }

    ACE_Thread_Manager::instance()->resume_grp(m_HandAskPBXGrp);
    ACE_Thread_Manager::instance()->resume(m_DispatchAskPBXID);

    return TRUE;
}

BOOL CHandleAskPBX::StopHandleAskPBX()
{
    m_AskMsgStop = true;
//    sem_post(&m_AskMsgRun);
    m_AskMsgRun.release();
    ACE_Thread_Manager::instance()->wait_grp(m_HandAskPBXGrp);
    m_AskMsgStop = false;
    m_HandAskPBXGrp = -1;

    m_DispatchStop = true;
//    sem_post(&m_DispatchRun);
    m_DispatchRun.release();
    ACE_Thread_Manager::instance()->join(m_DispatchAskPBXID);
    m_DispatchStop = false;
    m_DispatchAskPBXID = 0;

    m_AskMsgList.clear();

    return TRUE;
}

void* CHandleAskPBX::HandleAskPBX(LPVOID lpvoid)
{
    CHandleAskPBX *pHandleAskPBX = reinterpret_cast<CHandleAskPBX*>(lpvoid);

    while(true)
    {
//        sem_wait(&(pHandleAskPBX->m_AskMsgRun));
        pHandleAskPBX->m_AskMsgRun.acquire();
        if(pHandleAskPBX->m_AskMsgStop)
        {
            pHandleAskPBX->m_hSerialEvent.release();
            break;
        }
        else
            pHandleAskPBX->RunHandleAskPBX();
    }
    return NULL;
}

void* CHandleAskPBX::DispatchAskPBX(LPVOID lpvoid)
{
    CHandleAskPBX *pHandleAskPBX = reinterpret_cast<CHandleAskPBX*>(lpvoid);

    while(true)
    {
//        sem_wait(&(pHandleAskPBX->m_DispatchRun));
        pHandleAskPBX->m_DispatchRun.acquire();
        if(pHandleAskPBX->m_DispatchStop)
            break;
        else
            pHandleAskPBX->RunDispatchAskPBX();
    }
    return NULL;
}


void  CHandleAskPBX::RunHandleAskPBX()
{
    std::string strEvt;
    while(true)
    {
        m_AskMsgCritical.acquire();
        if(m_AskMsgList.empty())
        {
            m_AskMsgCritical.release();
            break;
        }
        strEvt = m_AskMsgList.front();
        m_AskMsgList.pop_front();
        int ncount = m_AskMsgList.size();
        m_AskMsgCritical.release();

        m_pDlg->m_Log.Log("接收到PBX的数据：\n--------------------------\n%s\n------------------------------\n", strEvt.c_str());
        m_pDlg->m_Log.Log("wait messages %d", ncount);

        m_hSerialEvent.acquire();
        ProcessEvt(strEvt);
        m_hSerialEvent.release();
    }
}

void CHandleAskPBX::ProcessEvt(const std::string &strEvt)
{
    CGeneralUtils  myGeneralUtils;

    char szResponse[64];
    memset(szResponse,0,64);
    myGeneralUtils.GetStringValue(strEvt,"Response",szResponse);

    char szEvent[64];
    memset(szEvent,0,64);
    myGeneralUtils.GetStringValue(strEvt,"Event",szEvent);
    //以上分别为Response 和 Event

    if(strcmp(szEvent,"") != 0)
    {
        char szEvent[128];
        memset(szEvent,0,128);
        myGeneralUtils.GetStringValue(strEvt,"Event",szEvent);

        if(strcmp(szEvent,"QueueMemberAdded") == 0)
        {
            //登录成功事件
            m_pDlg->m_Log.Log("Add QueueMember");
            //HandleLoginEvt(strEvt);
        }
        else if(strcmp(szEvent,"QueueMemberRemoved") == 0)
        {
            //登出成功事件
            m_pDlg->m_Log.Log("Remove QueueMember");
            HandleIVRLogout(strEvt);
        }
        else if(strcmp(szEvent,"QueueMemberPaused") == 0)
        {
            //空闲成功事件+示忙后处理成功事件
            m_pDlg->m_Log.Log("QueueMemberPaused");
            HandleQueueMemberPausedEvt(strEvt);
        }
        else if(strcmp(szEvent,"OriginateResponse") == 0)
        {
            //呼出失败事件
            m_pDlg->m_Log.Log("OriginateResponse");
            HandleOriginateResponseEvt(strEvt);
        }
        else if(strcmp(szEvent,"Dial") == 0)
        {
            //拨号成功事件
            m_pDlg->m_Log.Log("Dial");
            HandleDialEvt(strEvt);
        }
        else if(strcmp(szEvent,"Bridge") == 0)
        {
            //通话建立事件
            m_pDlg->m_Log.Log("Bridge");
            HandleEstablishEvt(strEvt);
        }
        else if(strcmp(szEvent,"Newstate") == 0)
        {
            //震铃事件
            m_pDlg->m_Log.Log("Newstate");
            HandleNewstateEvt(strEvt);
        }
        else if(strcmp(szEvent,"QueueMemberStatus") == 0)
        {
            //座席状态事件
            m_pDlg->m_Log.Log("QueueMemberStatus");
            HandQueueMemberStatusEvt(strEvt);
        }
        else if(strcmp(szEvent,"Hangup") == 0)
        {
            //挂机事件
            m_pDlg->m_Log.Log("Hangup");
            HandleHangupEvt(strEvt);
        }
        else if(strcmp(szEvent,"MeetmeJoin") == 0)
        {
            //进入会议事件
            m_pDlg->m_Log.Log("MeetmeJoin");
            HandleConfEvt(strEvt);
        }
        else if(strcmp(szEvent,"QueueParams") == 0)
        {
            //skillsnap事件，即技能组快照事件
            m_pDlg->m_Log.Log("QueueParams");
            HandleQueueStaticEvt(strEvt);
        }
        else if(strcmp(szEvent,"UserEvent") == 0)
        {
            //非Astrisk自带的事件
            m_pDlg->m_Log.Log("UserEvent");
            HandleUserEvent(strEvt);
        }
        else if(strcmp(szEvent,"Join") == 0)
        {
            //进入队列事件：呼入队列产生
            m_pDlg->m_Log.Log("Join");
            HandleJoinEvt(strEvt);
        }
        else if(strcmp(szEvent,"Leave") == 0)
        {
            //离开队列：分配到座席产生 skillRecord|
            m_pDlg->m_Log.Log("Leave");
            HandleLeaveEvt(strEvt);
        }
    }
    else if(strcmp(szResponse,"") != 0)
    {
        //Response: ...
        char szActionID[128];
        memset(szActionID,0,128);
        myGeneralUtils.GetStringValue(strEvt,"ActionID",szActionID);

        if(strcmp(szActionID, "") != 0)
        {
            //每个动作会产生一个response事件
            m_pDlg->m_Log.Log("response");
            HandleResponse(strEvt);
	    std::cout<<strEvt.c_str()<<std::endl;
        }
    }
}

void CHandleAskPBX::RunDispatchAskPBX()
{
    while(!m_pDlg->m_PBXMsgList.empty())
    {

//        std::string strEvt;
        std::string sTmp;
//        pthread_mutex_lock(&m_pDlg->m_PBXMsgCritical);
        m_pDlg->m_PBXMsgCritical.acquire();
        sTmp = m_pDlg->m_PBXMsgList.front();
        m_pDlg->m_PBXMsgList.pop_front();
        m_pDlg->m_PBXMsgCritical.release();
//        pthread_mutex_unlock(&m_pDlg->m_PBXMsgCritical);

        m_strEvtPBX += sTmp;

        size_t n = m_strEvtPBX.find("\r\n\r\n");
        size_t index;
        while(n != std::string::npos)
        {
            std::string strMsg = m_strEvtPBX.substr(0, n);

            index = strMsg.find(": ");
            while (index != std::string::npos)
            {
                strMsg.replace(index, strlen(": "), "=");
                index = strMsg.find(": ");
            }

            index = strMsg.find("\r\n");
            while (index != std::string::npos)
            {
                strMsg.replace(index, strlen("\r\n"), ";");
                index = strMsg.find("\r\n");
            }

//            pthread_mutex_lock(&m_AskMsgCritical);
            m_AskMsgCritical.acquire();
            m_AskMsgList.push_back(strMsg);
//            pthread_mutex_unlock(&m_AskMsgCritical);
            m_AskMsgCritical.release();
//            sem_post(&m_AskMsgRun);
            m_AskMsgRun.release();
            int nLen = n+4;
            m_strEvtPBX.erase(0, nLen);
            n = m_strEvtPBX.find("\r\n\r\n");
        }
    }
}

BOOL CHandleAskPBX::SetAgentStatusReportData(CAgent* pAgent)
{
    std::string sAgentStatus = m_pDlg->m_strAgentStatusWeb;
    if(pAgent)
    {
        std::string sCmd="agentStatus|";
        CUntiTool tool;
        std::string sCurrTime = tool.GetCurrTime();

        std::string sStatusCode;
        char format[128];
        memset(format, 0, 128);
        snprintf(format, 127, "%d", pAgent->AgentState);
        sStatusCode = format;

//		sStatusCode.Format("%d",pAgent->AgentState);
        std::string sStatusName = GetStatusNameFromCode(pAgent->AgentState);
//		CString sAgentStatus = m_pDlg->m_strAgentStatusWeb;

        if(pAgent->AgentState == AS_Login)
        {
            REPLACE(sAgentStatus, "[agentId]",pAgent->sWorkNo);
            REPLACE(sAgentStatus, "[extension]",pAgent->sStation);
            REPLACE(sAgentStatus, "[skill]",pAgent->m_tempSkill);
            //sAgentStatus.Replace("[ringTime]","null");
            REPLACE(sAgentStatus, "[startTime]",sCurrTime);
            REPLACE(sAgentStatus, "[statusCode]",sStatusCode);
            REPLACE(sAgentStatus, "[statusName]",sStatusName);
            REPLACE(sAgentStatus, "[statusReason]","nu");
            REPLACE(sAgentStatus, "[tenantId]",pAgent->sTeantid);
            std::string sData = "";
            sData += sCmd;
            sData += sAgentStatus;
            pAgent->m_strAgentStatusReport = sData;
        }
        else
        {

            CUntiTool tool;
            std::string sCurrTime = tool.GetCurrTime();
            REPLACE(pAgent->m_strAgentStatusReport, "[endTime]",sCurrTime);

            //m_pDlg->m_Log.Log(FLOW_LOG,"小休报表字段2:%s",pAgent->m_strAgentStatusReport);
            //m_pDlg->m_Log.Log(FLOW_LOG,"agenStatus push");
            m_pDlg->m_WSQueue.Push(pAgent->m_strAgentStatusReport);
            m_pDlg->m_HandleWS.m_hStartHandleWebResponse.release();

            ///========================================================================

            pAgent->m_strAgentStatusReport = "";
            REPLACE(sAgentStatus, "[agentId]",pAgent->sWorkNo);
            REPLACE(sAgentStatus, "[extension]",pAgent->sStation);
            REPLACE(sAgentStatus, "[skill]",pAgent->m_tempSkill);
            REPLACE(sAgentStatus, "[startTime]",sCurrTime);
            REPLACE(sAgentStatus, "[statusCode]",sStatusCode);
            REPLACE(sAgentStatus, "[statusName]",sStatusName);
            //////////////////////////////////////////////////////////////////////////
            //2013-01-22 edit by chenlin
            //只有小休、挂机有原因，其他的没有原因置为nu
            //m_pDlg->m_Log.Log(FLOW_LOG,"小休的原因码%s",pAgent->sReasonCode);
            if(pAgent->AgentState == AS_Notready && pAgent->sReasonCode != "")
            {
                REPLACE(sAgentStatus, "[statusReason]",pAgent->sReasonCode);
            }
            else
            {
                REPLACE(sAgentStatus, "[statusReason]","nu");
            }
            //m_pDlg->m_Log.Log(FLOW_LOG,"小休报表字段1:%s",sAgentStatus);
            //////////////////////////////////////////////////////////////////////////

            REPLACE(sAgentStatus, "[tenantId]",pAgent->sTeantid);

            //根据电信的情况要求追加的
            if(pAgent->AgentState == AS_Active)
            {
                std::string tmp = "&ani=" + pAgent->sAni + "&dnis=" + pAgent->sDnis + "&calldirect=" + pAgent->sCallDirect;
                //tmp.Format("&ani=%s&dnis=%s&calldirect=%s",pAgent->sAni,pAgent->sDnis,pAgent->sCallDirect);
                sAgentStatus+=tmp;
            }
            /************************************************************************/
            //2013-03-12 add by chenlin

            if (pAgent->AgentState == AS_Wrap)
            {
                //后处理附加呼叫方向
                //AgentStatus.Replace("[calldirect]",pAgent->sCallDirect);
                std::string strAdd = "&calldirect=" + pAgent->orginCallDirect;
                //strAdd.Format("&calldirect=%s",pAgent->orginCallDirect);
                sAgentStatus += strAdd;
            }
            /************************************************************************/
            std::string sMsg = sCmd + sAgentStatus;
            //sMsg += sCmd;
            //sMsg += sAgentStatus;

            pAgent->m_strAgentStatusReport = sMsg;
            //m_pDlg->m_Log.Log(FLOW_LOG,"小休报表字段2:%s",pAgent->m_strAgentStatusReport);
        }
        //////////////////////////////////////////////////////////////////////////
        //2013-01-22 add by chenlin
        //para="agentId=[agentId]&extension=[extension]&skill=[skill]&startTime=[startTime]
        //&statusCode=[statusCode]&statusName=[statusName]&statusReason=[statusReason]&tenantId=[tenantId]" />
        std::string strAgentActualTimeStatus = m_pDlg->m_strAgentActualTimeStatus;
        REPLACE(strAgentActualTimeStatus, "[termiantype]",pAgent->m_strSource);
        REPLACE(strAgentActualTimeStatus, "[agentId]",pAgent->sWorkNo);
        REPLACE(strAgentActualTimeStatus, "[extension]",pAgent->sStation);
        REPLACE(strAgentActualTimeStatus, "[skill]",pAgent->m_tempSkill);
        REPLACE(strAgentActualTimeStatus, "[ringTime]","nu");
        REPLACE(strAgentActualTimeStatus, "[startTime]",sCurrTime);
        REPLACE(strAgentActualTimeStatus, "[statusCode]",sStatusCode);
        REPLACE(strAgentActualTimeStatus, "[statusName]",sStatusName);
        //只有小休、挂机有原因，其他的没有原因置为nu
        //m_pDlg->m_Log.Log(FLOW_LOG,"小休的原因码%s",pAgent->sReasonCode);
        if(pAgent->AgentState == AS_Notready && pAgent->sReasonCode != "")
        {
            REPLACE(strAgentActualTimeStatus, "[statusReason]",pAgent->sReasonCode);
        }
        else
        {
            REPLACE(strAgentActualTimeStatus, "[statusReason]","nu");
        }
        //m_pDlg->m_Log.Log(FLOW_LOG,"小休报表字段1:%s",sAgentStatus);

        if(pAgent->AgentState == AS_Active)
        {
            //通话的时候才有主被叫
            std::string tmp = "&ani=" + pAgent->sAni + "&dnis=" + pAgent->sDnis + "&calldirect=" + pAgent->sCallDirect;
            //tmp.Format("&ani=%s&dnis=%s&calldirect=%s",pAgent->sAni,pAgent->sDnis,pAgent->sCallDirect);
            strAgentActualTimeStatus+=tmp;
        }

        REPLACE(strAgentActualTimeStatus, "[tenantId]",pAgent->sTeantid);

        std::string cmd = "agentActualTimeStatus|";
        strAgentActualTimeStatus = cmd + strAgentActualTimeStatus;
        m_pDlg->m_WSQueue.Push(strAgentActualTimeStatus);

        //////////////////////////////////////////////////////////////////////////

    }
    return TRUE;
}

std::string CHandleAskPBX::GetStatusNameFromCode(int callStatus)
{
    if(callStatus == AS_Unknown)
    {
        return "AS_Unknown";
    }
    else if(callStatus == AS_Notlogin)
    {
        return "AS_Notlogin";
    }
    else if(callStatus == AS_Login)
    {
        return "AS_Login";
    }
    else if(callStatus == AS_Idle)
    {
        return "AS_Idle";
    }
    else if(callStatus == AS_Notready)
    {
        return  "AS_Notready";
    }
    else if(callStatus == AS_Alerting)
    {
        return "AS_Alerting";
    }
    else if(callStatus == AS_Dial)
    {
        return  "AS_Dial";
    }
    else if(callStatus == AS_DialAgent)
    {
        return "AS_DialAgent";
    }
    else if(callStatus == AS_Active)
    {
        return "AS_Active";

    }
    else if(callStatus == AS_Preview)
    {
        return "AS_Preview";
    }
    else if(callStatus == AS_Hold)
    {
        return "AS_Hold";
    }
    else if(callStatus == AS_Consulting)
    {
        return "AS_Consulting";
    }
    else if(callStatus == AS_Conferencing)
    {
        return "AS_Conferencing";
    }
    else if(callStatus == AS_Manage)
    {
        return "AS_Manage";
    }
    else if(callStatus == AS_Monitor)
    {
        return "AS_Monitor";
    }
    else if(callStatus == AS_HoldUp)
    {
        return "AS_HoldUp";
    }
    else if(callStatus == AS_Observe)
    {
        return "AS_Observe";
    }
    else if(callStatus == AS_Wrap)
    {
        return "AS_Wrap";
    }
    else if(callStatus == AS_Hangup)
    {
        return "AS_Hangup";
    }
    else if (callStatus == AS_Transfer)
    {
        return "AS_Transfer";
    }
    else
    {
        return "null";
    }
}

BOOL CHandleAskPBX::SetWSSkillSnap(CQueueEvt* pQueue, const std::string &sTenantID)
{
    std::string sCmd("skillSnap|");
    CUntiTool tool;
    std::string sCurrTime = tool.GetCurrTime();

    std::string sData = m_pDlg->m_strskillSnapWeb;

    REPLACE(sData, "[createTime]", sCurrTime);
//	sData.Replace("[createTime]",sCurrTime);

    REPLACE(sData, "[tenantId]",sTenantID);
//	sData.Replace("[tenantId]",sTenantID);

    REPLACE(sData, "[skill]",pQueue->sQueue);
//	sData.Replace("[skill]",pQueue->sQueue);

    REPLACE(sData, "[waitingCalls]",pQueue->sWaitCalls);
//	sData.Replace("[waitingCalls]",pQueue->sWaitCalls);

    REPLACE(sData, "[agentActiveCount]",pQueue->sActiveCalls);
//	sData.Replace("[agentActiveCount]",pQueue->sActiveCalls);//add by ly

    REPLACE(sData, "[discardPhoneCount]",pQueue->sDiscardCalls);
//	sData.Replace("[discardPhoneCount]",pQueue->sDiscardCalls);

    REPLACE(sData, "[agentCallCount]",pQueue->sCompleteCalls);
//	sData.Replace("[agentCallCount]",pQueue->sCompleteCalls);

    REPLACE(sData, "[agentAvailableCount]",pQueue->sagentAvailableCount);
//	sData.Replace("[agentAvailableCount]",pQueue->sagentAvailableCount);

    REPLACE(sData, "[pausedMembers]",pQueue->spausedMembers);
//	sData.Replace("[pausedMembers]",pQueue->spausedMembers);

    std::string sMsg(sCmd+sData);
//	sMsg += sCmd;
//	sMsg += sData;

//	m_pDlg->m_skillSnapLog.Log(FLOW_LOG,"skillSnap push");//更改，因为有记录
    m_pDlg->m_WSQueue.Push(sMsg);
//	SetEvent(m_pDlg->m_HandleWS.m_hStartHandleWebResponse);
    m_pDlg->m_HandleWS.m_hStartHandleWebResponse.release();

    return 1;
}

BOOL CHandleAskPBX::HandleQueueMemberPausedEvt(const std::string &strEvt)
{
    CGeneralUtils myGeneralUtils;

    char szMemberName[64];
    memset(szMemberName,0,64);
    myGeneralUtils.GetStringValue(strEvt,"MemberName",szMemberName);

    char szPauss[64];
    memset(szPauss,0,64);
    myGeneralUtils.GetStringValue(strEvt,"Paused",szPauss);

    CUntiTool tool;
    std::string strStation = tool.GetStationFromMemberName(szMemberName);

    BOOL bRet = FALSE;

    //m_pDlg->SetLock();
    CAgent* pAgent = m_pDlg->GetAgentFromStation(strStation);


    if(!pAgent)
    {
        m_pDlg->m_Log.Log("FLOW_LOG->QueueMemberPaused:no find agent %s",strStation.c_str());
        bRet = FALSE;
    }
    else
    {
        std::string strAskMsg;
        //if(pAgent->sAction == "SetIdle")
        if(strcmp(szPauss, "0") == 0)
        {
            CIdleEvt msg;
            //------------------ 2013-07-02 --------------------------
            msg.m_strStation = pAgent->sStation;
            msg.m_strAgentId = pAgent->sWorkNo;
            CUntiTool tool;
            msg.m_strTime = tool.GetCurrTime();

            msg.m_strRet = "Succ";
            msg.m_strUserData = "station set idle succ";
            strAskMsg = msg.EnSoftPhoneMsg();
            pAgent->lock();
            pAgent->AgentState = AS_Idle;
            SetAgentStatusReportData(pAgent);
            pAgent->unlock();

            CAgentOp op;
            op.SendResult(pAgent,strAskMsg);
            m_pDlg->m_Log.Log("FLOW_LOG->QueueMemberPaused:station=%s-%s-idle succ",pAgent->sStation.c_str(),pAgent->sAction.c_str());
        }
        else if(pAgent->sAction == "SetBusy" && strcmp(szPauss, "1") == 0)
        {
            CBusyEvt evt;
            //------------------ 2013-07-02 --------------------------
            CUntiTool tool;
            evt.m_strTime = tool.GetCurrTime();
            evt.m_strStation = pAgent->sStation;
            evt.m_strAgentId = pAgent->sWorkNo;
            evt.m_strRet = "Succ";
            evt.m_strUserData = "station set busy succ";
            strAskMsg = evt.EnSoftPhoneMsg();
            pAgent->lock();
            pAgent->AgentState = AS_Notready;
            SetAgentStatusReportData(pAgent);
            pAgent->unlock();
            //		Sleep(0);
            
            CAgentOp op;
            op.SendResult(pAgent,strAskMsg);
            m_pDlg->m_Log.Log("FLOW_LOG->QueueMemberPaused:station=%s-%s-busy succ",pAgent->sStation.c_str(),pAgent->sAction.c_str());

        }
        else if(pAgent->sAction == "SetWrapup")
        {
            CWrapupEvt evt;
            //------------------ 2013-07-02 --------------------------
            CUntiTool tool;
            evt.m_strTime = tool.GetCurrTime();
            evt.m_strStation = pAgent->sStation;
            evt.m_strAgentId = pAgent->sWorkNo;
            evt.m_strRet = "Succ";
            evt.m_strUserData = "station set wrapup succ";
            strAskMsg = evt.EnSoftPhoneMsg();
            pAgent->lock();
            pAgent->AgentState = AS_Wrap;
            SetAgentStatusReportData(pAgent);
            pAgent->unlock();
            usleep(10*1000);
            CAgentOp op;
            op.SendResult(pAgent,strAskMsg);
            m_pDlg->m_Log.Log("FLOW_LOG->QueueMemberPaused:station=%s-%s-succ",pAgent->sStation.c_str(),pAgent->sAction.c_str());
            usleep(10*1000);

        }
        bRet = TRUE;
        pAgent->Release();
    }

    //m_pDlg->SetUnLock();

    return bRet;
}

BOOL CHandleAskPBX::SetWSCDRData(CAgent* pAgent)
{
    std::string sCmd="cdr|";

    CUntiTool tool;

    std::string sCurrTime = tool.GetCurrTime();

//	CString sData = m_pDlg->m_strCdrWeb;
//	m_pDlg->m_Log.Log(TEST_LOG,"@@@测试测试CDR:pAgent,state=%d;station=%s;bIntoEstablish=%d",pAgent->AgentState,pAgent->sStation,pAgent->bIntoEstablish);
    if(pAgent->AgentState == AS_Alerting)
    {
        //第一次接听的时间:振铃时间
        //int nPos = pAgent->m_strAgentCDRReport.Replace("[ringTime]",sCurrTime);
        size_t nPos = pAgent->m_strAgentCDRReport.find("[ringTime]");

        if(nPos != std::string::npos)
        {
            REPLACE(pAgent->m_strAgentCDRReport, "[ringTime]",sCurrTime);
            pAgent->orginAni = pAgent->sAni;
            pAgent->orginDnis = pAgent->sDnis;
            pAgent->orginCallDirect = pAgent->sCallDirect;
            pAgent->originUCID = pAgent->sUCID;
            //////////////////////////////////////////////////////////////////////////
            //2013-01-21 add by chenlin
            pAgent->m_callRingTime = sCurrTime;
            //////////////////////////////////////////////////////////////////////////
        }
    }
    else if(pAgent->AgentState == AS_Dial)
    {
        //第一次拨号成功的的时间:是指对方振铃的时间
        //int nPos = pAgent->m_strAgentCDRReport.Replace(_T("[ringTime]"),sCurrTime);
        size_t nPos = pAgent->m_strAgentCDRReport.find("[ringTime]");

        if(nPos != std::string::npos)
        {
            REPLACE(pAgent->m_strAgentCDRReport, "[ringTime]", sCurrTime);
            pAgent->orginAni = pAgent->sAni;
            pAgent->orginDnis = pAgent->sDnis;
            pAgent->orginCallDirect = pAgent->sCallDirect;
            pAgent->originUCID = pAgent->sUCID;
            //////////////////////////////////////////////////////////////////////////
            //2013-01-21 add by chenlin
            pAgent->m_callRingTime = sCurrTime;
            //////////////////////////////////////////////////////////////////////////
        }

    }
    else if(pAgent->AgentState == AS_Monitor || pAgent->AgentState == AS_Observe)
    {
        //int nPos = pAgent->m_strAgentCDRReport.Replace(_T("[ringTime]"),sCurrTime);
        //int nPos = pAgent->m_strAgentCDRReport.find(_T("[ringTime]"));
        REPLACE(pAgent->m_strAgentCDRReport, _T("[ringTime]"),sCurrTime);
        //nPos = pAgent->m_strAgentCDRReport.Replace(_T("[startTime]"),sCurrTime);
        //nPos = pAgent->m_strAgentCDRReport.find(_T("[startTime]"));
        REPLACE(pAgent->m_strAgentCDRReport, _T("[startTime]"),sCurrTime);
        pAgent->bIntoEstablish = TRUE;
        //////////////////////////////////////////////////////////////////////////
        //2013-01-21 add by chenlin
        pAgent->m_callRingTime = sCurrTime;
        pAgent->m_callStartTime = sCurrTime;
        //////////////////////////////////////////////////////////////////////////
    }
    else if(pAgent->AgentState == AS_Active)
    {
        //第一次通话建立的时间:双方建立通话，注意下面如果没有建立通话，则将这个值置为""

        //int nPos = pAgent->m_strAgentCDRReport.Replace(_T("[startTime]"),sCurrTime);
        size_t nPos = pAgent->m_strAgentCDRReport.find(_T("[startTime]"));

        if(nPos != std::string::npos)
        {
            REPLACE(pAgent->m_strAgentCDRReport, _T("[startTime]"),sCurrTime);
            //////////////////////////////////////////////////////////////////////////
            //2013-01-21 add by chenlin
            pAgent->m_callStartTime = sCurrTime;
            //////////////////////////////////////////////////////////////////////////
            pAgent->bIntoEstablish = TRUE;
        }
    }
    else if(pAgent->AgentState == AS_Hangup)
    {
        //m_pDlg->m_Log.Log(TEST_LOG,"@@@@@测试AS_Hangup-bIntoEstablish=%d",pAgent->bIntoEstablish);
        //判断是否进入了通话,如果没有，开始时间==结束时间
        if(!pAgent->bIntoEstablish)
        {
            REPLACE(pAgent->m_strAgentCDRReport, _T("[startTime]"),sCurrTime);
            //////////////////////////////////////////////////////////////////////////
            //2013-01-21 add by chenlin
            pAgent->m_callStartTime = sCurrTime;
            //////////////////////////////////////////////////////////////////////////
        }
        REPLACE(pAgent->m_strAgentCDRReport, _T("[ucid]"),pAgent->originUCID);
        REPLACE(pAgent->m_strAgentCDRReport, _T("[agentId]"),pAgent->sWorkNo);
        REPLACE(pAgent->m_strAgentCDRReport, _T("[extension]"),pAgent->sStation);
        //////////////////////////////////////////////////////////////////////////
        //2013-03-06 edit by chenlin
        m_pDlg->m_Log.Log("TEST_LOG->发送cdr-sQueueName=%s;m_tempSkill=%s*",pAgent->sQueueName.c_str(), pAgent->m_tempSkill.c_str());
        if (pAgent->sQueueName == "")
        {
            REPLACE(pAgent->m_strAgentCDRReport, _T("[skill]"), pAgent->m_tempSkill);
        }
        else
        {
            REPLACE(pAgent->m_strAgentCDRReport, _T("[skill]"),pAgent->sQueueName);
        }

// 		if (pAgent->m_tempSkill != "")
// 		{
// 			pAgent->m_strAgentCDRReport.Replace(_T("[skill]"), pAgent->m_tempSkill);
// 		}else
// 		{
// 			pAgent->m_strAgentCDRReport.Replace(_T("[skill]"),pAgent->sQueueName);
// 		}

        //////////////////////////////////////////////////////////////////////////
        REPLACE(pAgent->m_strAgentCDRReport, _T("[callType]"),pAgent->orginCallDirect);
        REPLACE(pAgent->m_strAgentCDRReport, _T("[caller]"),pAgent->orginAni);
        REPLACE(pAgent->m_strAgentCDRReport, _T("[called]"),pAgent->orginDnis);
        REPLACE(pAgent->m_strAgentCDRReport, _T("[endTime]"),sCurrTime);
        REPLACE(pAgent->m_strAgentCDRReport, _T("[tenantId]"),pAgent->sTeantid);
        REPLACE(pAgent->m_strAgentCDRReport, _T("[callReason]"),"un");//
        REPLACE(pAgent->m_strAgentCDRReport, _T("[callEndReason]"),"un");//,pAgent->sReasonCode);
        //////////////////////////////////////////////////////////////////////////
        //2013-01-21 add by chenlin
        if(pAgent->m_strSource == "TelSoftPhone")
        {
            pAgent->m_callClearTime = sCurrTime;
            pAgent->savePopData();
        }

        //////////////////////////////////////////////////////////////////////////
        std::string sData = sCmd;
        sData += pAgent->m_strAgentCDRReport;

        m_pDlg->m_Log.Log("FLOW_LOG->cdr push");
        m_pDlg->m_WSQueue.Push(sData);

        //SetEvent(m_pDlg->m_HandleWS.m_hStartHandleWebResponse);
        m_pDlg->m_HandleWS.m_hStartHandleWebResponse.release();

        pAgent->orginAni = "";
        pAgent->orginDnis = "";
        pAgent->bIntoEstablish = FALSE;

    }
    return TRUE;
}

BOOL CHandleAskPBX::HandleHangupEvt(const std::string &strEvt)
{
	
    std::cout<<strEvt.c_str()<<std::endl;
    CGeneralUtils myGeneralUtil;
    char szChannel[64];
    memset(szChannel,0,64);
    myGeneralUtil.GetStringValue(strEvt,"Channel",szChannel);
//////////////////////////////////////////////////////////////////////////
    char szCallerIDNum[64];
    memset(szCallerIDNum,0,64);
    myGeneralUtil.GetStringValue(strEvt,"CallerIDNum",szCallerIDNum);
    m_pDlg->m_Log.Log("TEST_LOG->挂机事件，看谁挂机：%s",szCallerIDNum);
//////////////////////////////////////////////////////////////////////////
    //m_pDlg->SetLock();

	char szPhoneNo[64];
	memset(szPhoneNo,0,64);
	myGeneralUtil.GetStringValue(strEvt,"phoneno",szPhoneNo);
	std::cout<<"ext "<<szPhoneNo<<std::endl;
	if(strcmp(szPhoneNo,"")!=0)
	{
		std::cout<<"XXXXXXXXXXXXXXXXXXXXXXXXXXX"<<std::endl;
		
        char szCause[63];
        memset(szCause,0,63);
        myGeneralUtil.GetStringValue(strEvt,"Cause",szCause);

		char ucid[256];
		memset(ucid,0,256);
		myGeneralUtil.GetStringValue(strEvt,"ucid",ucid);
    
		char szSock[64];
		memset(szSock,0,64);
		myGeneralUtil.GetStringValue(strEvt,"sock",szSock);

		char szCmd[64];
		memset(szCmd,0,64);
		myGeneralUtil.GetStringValue(strEvt,"cmd",szCmd);


		std::string no(szPhoneNo);


		//std::cout<<CUntiTool::GetLocalAddr()<<std::endl;

		m_pDlg->m_phoneMapCritical.acquire();
		std::cout<<"size == "<<m_pDlg->m_phoneMap.size()<<std::endl;
		std::map<std::string,CPhone*>::iterator it = m_pDlg->m_phoneMap.find(no);

		if(it != m_pDlg->m_phoneMap.end())
		{
			CPhone* pPhone = (*it).second;
			//Succ
			std::string msg;
			if(strcmp(szCause,"16")== 0)
			{
				std::cout<<"OBS SUCC"<<std::endl;
			

				if(strcmp(szCmd,"VoiceCampaign") == 0)
				{

					std::cout<<"Enter Hangup Return Msg"<<std::endl;
					
					msg = "{\"Event\":\"VoiceCampaignResult\",";
					msg += std::string("\"CampaignID\":")+std::string("\"")+std::string(ucid)+std::string("\",");
					msg += std::string("\"PhoneNo\":")+std::string("\"")+std::string(szPhoneNo)+std::string("\",");
					msg += std::string("\"Result\":")+std::string("\"1\"}");	
					std::cout<<msg<<std::endl;
				}
				else if(strcmp(szCmd,"HumanRecord") == 0)
				{
					
					std::string httpPath;
					httpPath += "http://";
					httpPath += m_pDlg->m_SettingData.m_strPBX;
					httpPath += "/";
					httpPath += std::string(ucid);
					httpPath += std::string(".wav");
					
					msg = "{\"Event\":\"VoiceRecordResult\",";
					msg += std::string("\"CampaignID\":")+std::string("\"")+std::string(ucid)+std::string("\",");
					msg += std::string("\"VoiceFile\":")+std::string("\"")+httpPath+std::string("\",");
					msg += std::string("\"Result\":")+std::string("\"1\"}");	
				}
			}
			else
			{
			
				if(strcmp(szCmd,"VoiceCampaign") == 0)
				{
					msg = "{\"Event\":\"VoiceCampaignResult\",";
					msg += std::string("\"CampaignID\":")+std::string("\"")+std::string(ucid)+std::string("\",");
					msg += std::string("\"PhoneNo\":")+std::string("\"")+std::string(szPhoneNo)+std::string("\",");
					msg += std::string("\"Result\":")+std::string("\"0\"}");	
				}
				else if(strcmp(szCmd,"HumanRecord") == 0)
				{
					msg = "{\"Event\":\"VoiceRecordResult\",";
					msg += std::string("\"CampaignID\":")+std::string("\"")+std::string(ucid)+std::string("\",");
					msg += std::string("\"VoiceFile\":")+std::string("\"")+std::string("\",");
					msg += std::string("\"Result\":")+std::string("\"0\"}");	
				}
			}
			m_pDlg->m_IOOpt.SendMsgToUser(atoi(szSock),msg);

			m_pDlg->m_phoneMap.erase(it);
			delete pPhone;
		}
		m_pDlg->m_phoneMapCritical.release();
	}

    
    CAgent* pAgent = m_pDlg->GetAgentFromChan(szChannel);
    if(pAgent)
    {
        m_pDlg->m_Log.Log("TEST_LOG->HandleHangupEvt:Ext=%s Chan=%s",pAgent->sStation.c_str(), szChannel);

        char szCause[63];
        memset(szCause,0,63);
        myGeneralUtil.GetStringValue(strEvt,"Cause",szCause);

        char szUserData[63];
        memset(szUserData,0,63);
        myGeneralUtil.GetStringValue(strEvt,"Cause-txt",szUserData);

	char szSock[64];
	memset(szSock,0,64);
	myGeneralUtil.GetStringValue(strEvt,"sock",szSock);

	std::cout<<"HangupEvt ,  sock = "<<szSock<<std::endl;
	m_pDlg->m_IOOpt.SendMsgToUser(atoi(szSock),std::string("xxxxxxx"));	

        CHangupEvt evt;
        evt.m_strReason = szCause;
        evt.m_strUserData = szUserData;
        evt.m_strUCID = pAgent->sUCID;
        //////////////////////////////////////////////////////////////////////////
        //2013-01-07 add by chenlin ↓
        //////////////////////////////////////////////////////////////////////////
        //为手机客户端存入通话结束时间
//
// 		COleDateTime ti = COleDateTime::GetCurrentTime();
// 		pAgent->m_callClearTime = ti.Format("%Y-%m-%d %H:%M:%S");

        //每次挂断时，将弹屏数据存入链表，供手机客户端调取
//		pAgent->savePopData();
        //////////////////////////////////////////////////////////////////////////
        //2013-01-07 add by chenlin ↑
        //////////////////////////////////////////////////////////////////////////


        //pAgent->sReasonCode = szCause;//通话系统送过来的挂断原因
        pAgent->lock();
        pAgent->AgentState = AS_Hangup;
        SetWSCDRData(pAgent);
        SetAgentStatusReportData(pAgent);
        CUntiTool tool;
        evt.m_strTime = tool.GetCurrTime();
        evt.m_strStation = pAgent->sStation;
        evt.m_strAgentId = pAgent->sWorkNo;
        pAgent->unlock();


        std::string strInfo = evt.EnSoftPhoneMsg();
        CAgentOp op;
        op.SendResult(pAgent,strInfo);
        m_pDlg->m_Log.Log("TEST_LOG->发送给座席的挂断事件：%s",strInfo.c_str());



        pAgent->lock();
        pAgent->connChan[0].ResetCONN();
        pAgent->connChan[1].ResetCONN();

        //@增加判断
        if (pAgent->m_strSource == "TelSoftPhone")
        {
            pAgent->SetIdle();
        }
        else if(pAgent->m_strSource == "S_WEB")
        {
            switch (pAgent->m_nDefStatus)
            {
            case AS_Idle:
                pAgent->SetIdle();
                break;
            case AS_Notready:
                pAgent->SetBusy();
                break;
            default:
                pAgent->SetWrapup();
                pAgent->AgentState = AS_Wrap;
                break;
            }
        }
        else// if  (pAgent->m_strSource == "C_WEB")
        {
            switch (pAgent->m_HangupStatus)
            {
            case AS_Idle:
                pAgent->SetIdle();
                break;
            case AS_Notready:
                pAgent->SetBusy();
                break;
            default:
                pAgent->SetWrapup();
                pAgent->AgentState = AS_Wrap;
                break;
            }
        }
//        else
//        {
//            //2013-06-04 客户端自定义挂断后的状态
//            switch (pAgent->m_nDefStatus)
//            {
//            case AS_Idle:
//                pAgent->SetIdle();
//                break;
//            case AS_Notready:
//                pAgent->SetBusy();
//                break;
//            default:
//                pAgent->SetWrapup();
//                pAgent->AgentState = AS_Wrap;
//                break;
//            }
//
//        }

        //Sleep(500);
        //////////////////////////////////////////////////////////////////////////
        //2013-01-09 edit by chenlin
        //@针对手机客户端需求：挂断直接进入空闲状态;二期调整
        //@增加判断
// 		if (pAgent->m_strSource == "TelSoftPhone")
// 		{
// 			pAgent->SetIdle();
// 		}
// 		else
// 		{
// 			pAgent->SetWrapup();
//
// 		}
        //////////////////////////////////////////////////////////////////////////
        //pAgent->SetWrapup();
        pAgent->reset_phone();
        //pAgent->AgentState = AS_Wrap;
        SetAgentStatusReportData(pAgent);
        pAgent->unlock();
        m_pDlg->RemoveChanFromMap(szChannel,pAgent);
        pAgent->Release();
    }
    else
    {
        m_pDlg->m_Log.Log("FLOW_LOG->HandleHangupEvt  Chan=%s,没有发现通道对应的分机",szChannel);
    }
    //auto it = m_pDlg->m_MapChanToAgent.find(szChannel);
    //if(it != m_pDlg->m_MapChanToAgent.end())
    //    m_pDlg->m_MapChanToAgent.erase(it);  //移除map表key值
    //m_pDlg->SetUnLock();




    return TRUE;
}

BOOL CHandleAskPBX::HandleOriginateResponseEvt(const std::string &strEvt)
{

    std::cout<<strEvt.c_str()<<std::endl;
    CGeneralUtils myGeneralUtil;

    char szAction[512];
    memset(szAction,0,512);
    myGeneralUtil.GetStringValue(strEvt,"ActionID",szAction);

    char szResponse[64];
    memset(szResponse,0,64);
    myGeneralUtil.GetStringValue(strEvt,"Response",szResponse);
	
    if(strcmp(szResponse,"Failure") == 0)
    {
	

    	std::string action(szAction);
    	size_t pos1 = action.find("|");
    	size_t pos2 = action.find("|",pos1+1);
    	size_t pos3 = action.find("|",pos2+1);
    	size_t pos4 = action.find("|",pos3+1);
    	size_t pos5 = action.find("|",pos4+1);

		std::string cmd = action.substr(0,pos1-1);
    	std::string no = action.substr(pos1+1,pos2-pos1-1);
    	std::string sock = action.substr(pos3+1,pos4-pos3-1);
    	std::string ucid = action.substr(pos4+1,pos5-pos4-1);
    	std::cout<<no<<" "<<sock<<" "<<ucid<<std::endl;

	m_pDlg->m_phoneMapCritical.acquire();
	
	std::map<std::string,CPhone*>::iterator it = m_pDlg->m_phoneMap.find(no);
	if(it != m_pDlg->m_phoneMap.end())
	{
		std::string msg ;
		if(cmd == "HumanRecord")
		{
			msg = "{\"Event\":\"VoiceRecordResult\",";
		}
		else
		{
			msg = "{\"Event\":\"VoiceCampaignResult\",";
		}
		msg += std::string("\"CampaignID\":")+std::string("\"")+std::string(ucid)+std::string("\",");
		if(cmd == "HumanRecord")
		{
			msg += std::string("\"VoiceFile\":","\"\"");
		}
		else
		{
			msg += std::string("\"PhoneNo\":")+std::string("\"")+no+std::string("\",");
		}
		msg += std::string("\"Result\":")+std::string("\"0\"}");	
		int s = atoi(sock.c_str());
		m_pDlg->m_IOOpt.SendMsgToUser(s,msg);
		
		CPhone* pPhone = (*it).second;
		delete pPhone;
		m_pDlg->m_phoneMap.erase(it);

	}
	m_pDlg->m_phoneMapCritical.release();
    }
	
    return TRUE;
	

// 	char szCaller[64];
// 	memset(szCaller,0,64);
// 	myGeneralUtil.GetStringValue(strEvt,"CallerIDNum",szCaller);

// 	char szExten[64];
// 	memset(szExten,0,64);
// 	myGeneralUtil.GetStringValue(strEvt,"Exten",szExten);






    std::string sAction(szAction);

    //发现Station
    size_t n1 = sAction.find("|",0);
    size_t n2 = sAction.find("|",n1+1);
    if(n1!=std::string::npos || n2!=std::string::npos)
        return FALSE;
    std::string strStation = sAction.substr(n1+1,n2-n1-1);

    if(strStation != "")
    {
        CAgent* pAgent = NULL;
        //m_pDlg->SetLock();
        pAgent = m_pDlg->GetAgentFromStation(strStation);
        if(pAgent != NULL)
        {
            if(pAgent->sAction == "MakeCall")
            {
                std::string strAskMsg;
                if(strcmp(szResponse,"Failure") == 0)
                {

//					char szReason[64];
// 										memset(szReason,0,64);
// 						                myGeneralUtil.GetStringValue(strEvt,"Reason",szReason);
// 					/*					CMakeCallEvt msg;
// 										msg.m_strCause = szReason;
// 										msg.m_strRet = "Fail";
// 										msg.m_strUserData = "make call fail";
// 										strAskMsg = msg.EnSoftPhoneMsg();
//
// 										pAgent->AgentState = AS_Dial;
// 										SetAgentStatusReportData(pAgent);
// 										SetWSCDRData(pAgent);
//
// 										CAgentOp op;
// 										op.SendResult(pAgent,strAskMsg);*/
                }
                else
                {
                    CMakeCallEvt msg;
                }
            }
            pAgent->Release();
        }
        else
        {
            m_pDlg->m_Log.Log("FLOW_LOG->HandleOriginateResponseEvt Not Find station",strStation.c_str());
        }
        //m_pDlg->SetUnLock();
    }
    else
    {
        m_pDlg->m_Log.Log("FLOW_LOG->HandleOriginateResponseEvt station is null");
    }

    return TRUE;
}

BOOL CHandleAskPBX::HandleNewstateEvt(const std::string &strEvt)
{
    CGeneralUtils myGeneralUtil;

    char szAccessNumber[32];
    memset(szAccessNumber,0,32);
    myGeneralUtil.GetStringValue(strEvt,"AccessNumber",szAccessNumber);

    char szState[32];
    memset(szState,0,32);
    myGeneralUtil.GetStringValue(strEvt,"ChannelStateDesc",szState);
    //如果被叫是座席
    if(strcmp(szState,"Ringing") == 0)
    {
        char szCalled[32];
        memset(szCalled,0,32);
        myGeneralUtil.GetStringValue(strEvt,"Called",szCalled);
        //m_pDlg->SetLock();
        CAgent* pAgent = m_pDlg->GetAgentFromStation(szCalled);
        if(pAgent)
        {

            char szChan[32];
            memset(szChan,0,32);
            myGeneralUtil.GetStringValue(strEvt,"Channel",szChan);

            char szCaller[32];
            memset(szCaller,0,32);
            myGeneralUtil.GetStringValue(strEvt,"Caller",szCaller);

            char szUCID[128];
            memset(szUCID,0,128);
            myGeneralUtil.GetStringValue(strEvt,"Ucid",szUCID);

            char szDirection[32];
            memset(szDirection,0,32);
            myGeneralUtil.GetStringValue(strEvt,"Direction",szDirection);

            pAgent->lock();
            pAgent->sAni = szCaller;
            pAgent->sDnis = szCalled;
            pAgent->AgentState = AS_Alerting;
            pAgent->sSelfChanID = szChan;
            pAgent->sCallDirect = szDirection;
            pAgent->sUCID = szUCID;

            //////////////////////////////////////////////////////////////////////////
            //2013-01-07 add by chenlin ↓
            //////////////////////////////////////////////////////////////////////////
            pAgent->m_callID = szChan; //Channel就是callID
            pAgent->m_accessNumber = szAccessNumber;
            pAgent->m_ivrTrack = szAccessNumber;//2012-11-30暂定ivr号和接入号一样
            pAgent->unlock();
            //@时间已移至SetWSCDRData函数——2013-01-21
// 			//在popdata数据中增加振铃时间
// 			COleDateTime ti = COleDateTime::GetCurrentTime();
// 			pAgent->m_callRingTime = ti.Format("%Y-%m-%d %H:%M:%S");

            //////////////////////////////////////////////////////////////////////////
            //2013-01-07 add by chenlin ↑
            //////////////////////////////////////////////////////////////////////////


            m_pDlg->m_Log.Log("FLOW_LOG->振铃 存入map：chan %s--分机%s",szChan,pAgent->sStation.c_str());
            //m_pDlg->m_MapChanToAgent.SetAt(szChan,(CObject*&)pAgent);
            //m_pDlg->m_MapChanToAgent[szChan] = pAgent;
            m_pDlg->SetAgentToChan(szChan, pAgent);
            pAgent->lock();
            SetAgentStatusReportData(pAgent);
            SetWSCDRData(pAgent);
            pAgent->unlock();
            CRingEvt evt;
            evt.m_strAni = szCaller;
            evt.m_strDnis = szCalled;
            evt.m_strDirect = szDirection;
            evt.m_strUCID = szUCID;
            evt.m_strIvrTrack = szAccessNumber;//弹屏数据增加IVR轨迹
            CUntiTool tool;
            evt.m_strTime = tool.GetCurrTime();
            evt.m_strStation = pAgent->sStation;
            evt.m_strAgentId = pAgent->sWorkNo;
            std::string sMsg = evt.EnSoftPhoneMsg();
            CAgentOp op;
            op.SendResult(pAgent,sMsg);
            pAgent->Release();

        }
        //m_pDlg->SetUnLock();
    }

    return TRUE;
}

BOOL CHandleAskPBX::HandQueueMemberStatusEvt(const std::string &strEvt)
{
    CGeneralUtils myGeneralUtil;

    char szQueueName[64];
    memset(szQueueName,0,64);
    myGeneralUtil.GetStringValue(strEvt,"Queue",szQueueName);

    if(strcmp(szQueueName,"") != 0)
    {
        char szMemberName[64];
        memset(szMemberName,0,64);
        myGeneralUtil.GetStringValue(strEvt,"MemberName",szMemberName);
        CUntiTool tool;
        std::string strStation = tool.GetStationFromMemberName(szMemberName);
        if(strStation != "")
        {
            CAgent* pAgent = NULL;
            //m_pDlg->SetLock();
            pAgent = m_pDlg->GetAgentFromStation(strStation);
            if(pAgent != NULL/* && pQueue!=NULL*/)
            {
                char szStatus[64];
                memset(szStatus,0,64);
                myGeneralUtil.GetStringValue(strEvt,"Status",szStatus);
                pAgent->lock();
                if(strcmp(szStatus,"3") == 0)
                {
                    pAgent->sAgentStatus = "3";
                }
                else
                {
                    if(pAgent->sAgentStatus == "3")
                    {
                        pAgent->sAgentStatus = "-1";
                    }
                }
                pAgent->unlock();
                pAgent->Release();
            }
            //m_pDlg->SetUnLock();
        }
    }
    return TRUE;
}

BOOL CHandleAskPBX::HandleDialEvt(const std::string &strEvt)
{
    BOOL bRet = FALSE;
    m_pDlg->m_Log.Log("TEST_LOG->HandDialEvt:%s",strEvt.c_str());
    CGeneralUtils myGeneralUtil;

    //////////////////////////////////////////////////////////////////////////
    //2013-01-07 add by chenlin
    char szAccessNumber[32];
    memset(szAccessNumber,0,32);
    myGeneralUtil.GetStringValue(strEvt,"AccessNumber",szAccessNumber);
    //////////////////////////////////////////////////////////////////////////

    char szChan[32];
    memset(szChan,0,32);
    myGeneralUtil.GetStringValue(strEvt,"Channel",szChan);

    char szCaller[32];
    memset(szCaller,0,32);
    myGeneralUtil.GetStringValue(strEvt,"Caller",szCaller);

    m_pDlg->m_Log.Log("TEST_LOG->caller=%s",szCaller);
    CUntiTool tool;
    //m_pDlg->SetLock();
    CAgent* pAgent = m_pDlg->GetAgentFromStation(szCaller);
    if(pAgent)
    {

        char szCalled[32];
        memset(szCalled,0,32);
        myGeneralUtil.GetStringValue(strEvt,"Called",szCalled);

        char szUCID[128];
        memset(szUCID,0,128);
        myGeneralUtil.GetStringValue(strEvt,"Ucid",szUCID);

	std::cout<<"ucid="<<szUCID<<std::endl;

        char szDirection[32];
        memset(szDirection,0,32);
        myGeneralUtil.GetStringValue(strEvt,"Direction",szDirection);

        char szTenantId[128];
        memset(szTenantId,0,128);
        myGeneralUtil.GetStringValue(strEvt,"TenantId",szTenantId);

        char szCalledGatewayCaller[32];//新增协议：只有当被叫号码为PSTN号码时，CalledGatewayCaller字段才被设置
        memset(szCalledGatewayCaller,0,32);
        myGeneralUtil.GetStringValue(strEvt,"CalledGatewayCaller",szCalledGatewayCaller);

        pAgent->lock();
        pAgent->sDnis = szCalled;

        std::string sCalledGatewayCaller = szCalledGatewayCaller;
        //sCalledGatewayCaller=szCalledGatewayCaller;
        /*
        if(sCalledGatewayCaller.IsEmpty()==FALSE)
        {//szCalledGatewayCaller该字段不为空
        	//<工号ID>,<租户ID>,<当前通话数>
            pAgent->sAni = szCalledGatewayCaller;
        }
        else
        {
        	pAgent->sAni = szCaller;
        }
        */

        pAgent->sAni = szCaller;


        pAgent->sCallDirect = szDirection;
        pAgent->sUCID = szUCID;
        pAgent->sTeantid = szTenantId;
        pAgent->sSelfChanID = szChan;
        pAgent->AgentState = AS_Dial;

        //////////////////////////////////////////////////////////////////////////
        //2013-01-07 add by chenlin ↓
        //////////////////////////////////////////////////////////////////////////
        pAgent->m_callID = szChan; //Channel就是callID
        pAgent->m_accessNumber = szAccessNumber;
        pAgent->m_ivrTrack = szAccessNumber;//2012-11-30暂定ivr号和接入号一样
        pAgent->unlock();
        //@时间已移至SetWSCDRData函数——2013-01-21
// 		//在popdata数据中增加振铃时间
// 		COleDateTime ti = COleDateTime::GetCurrentTime();
// 		pAgent->m_callRingTime = ti.Format("%Y-%m-%d %H:%M:%S");

        //////////////////////////////////////////////////////////////////////////
        //2013-01-07 add by chenlin ↑
        //////////////////////////////////////////////////////////////////////////

        m_pDlg->m_Log.Log("TEST_LOG->HandDialEvt:Ext=%s Chan=%s",pAgent->sStation.c_str(), szChan);


        //m_pDlg->m_MapChanToAgent.SetAt(szChan,(CObject*&)pAgent);
        m_pDlg->SetAgentToChan(szChan,pAgent);



        CMakeCallEvt evt;
        evt.m_strRet = "Succ";
        evt.m_strAni = szCaller;
        evt.m_strDnis = szCalled;
        evt.m_strUCID = szUCID;
        evt.m_strDirector = szDirection;
        CUntiTool tool;
        evt.m_strTime = tool.GetCurrTime();
        evt.m_strStation = pAgent->sStation;
        evt.m_strAgentId = pAgent->sWorkNo;

        CAgentOp op;
        op.SendResult(pAgent,evt.EnSoftPhoneMsg());

        pAgent->lock();
        SetWSCDRData(pAgent);
        SetAgentStatusReportData(pAgent);
        pAgent->unlock();
        pAgent->Release();

        bRet = TRUE;


    }
    else
    {
        bRet = FALSE;
        m_pDlg->m_Log.Log("FLOW_LOG->HandDialEvt:Ext=%s,Chan=%s,没有发现通道对应的分机",szCaller,szChan);
    }
    //m_pDlg->SetUnLock();

    return bRet;
}

BOOL CHandleAskPBX::HandleEstablishEvt(const std::string &strEvt)
{
    CGeneralUtils myGeneralUtil;

    char szBridgeState[32];
    memset(szBridgeState,0,32);
    myGeneralUtil.GetStringValue(strEvt,"Bridgestate",szBridgeState);

    if(strcmp(szBridgeState,"Unlink")==0)
	return 0;
    char szCaller[32];
    memset(szCaller,0,32);
    myGeneralUtil.GetStringValue(strEvt,"Caller",szCaller);


    char szCalled[32];
    memset(szCalled,0,32);
    myGeneralUtil.GetStringValue(strEvt,"Called",szCalled);


    char szUCID[128];
    memset(szUCID,0,128);
    myGeneralUtil.GetStringValue(strEvt,"Ucid",szUCID);

    char szChan1[64];
    memset(szChan1,0,64);
    myGeneralUtil.GetStringValue(strEvt,"Channel1",szChan1);

    char szChan2[64];
    memset(szChan2,0,64);
    myGeneralUtil.GetStringValue(strEvt,"Channel2",szChan2);

    char szDirect[32];
    memset(szDirect,0,32);
    myGeneralUtil.GetStringValue(strEvt,"Direction",szDirect);


    std::string tChan1 = CUntiTool::getValidChanFromOrigChan(szChan1);
    std::string tChan2 = CUntiTool::getValidChanFromOrigChan(szChan2);

    //tChan1.find("AsyncGoto/");
    
    m_pDlg->m_Log.Log("#################chan1 = %s ,  chan2 = %s ",tChan1.c_str(),tChan2.c_str());
    //m_pDlg->SetLock();
    CAgent* pCallerAgent = m_pDlg->GetAgentFromChan(tChan1);
    if(pCallerAgent)
    {

        CEstablishEvt evt;
        evt.m_strAni = szCaller;
        evt.m_strDnis = szCalled;
        evt.m_strUCID = szUCID;
        CUntiTool tool;
        evt.m_strTime = tool.GetCurrTime();
        evt.m_strStation = pCallerAgent->sStation;
        evt.m_strAgentId = pCallerAgent->sWorkNo;
        pCallerAgent->lock();

        pCallerAgent->sOtherID = szChan2;
        //////////////////////////////////////////////////////////////////////////
        //2013-03-18 add by chenlin
        //增加咨询状态给报表，深圳电信需求
        if (pCallerAgent->bConsult)
        {
            //说明是咨询通话，表示为咨询中状态
            pCallerAgent->AgentState = AS_Consulting;
            pCallerAgent->bConsult = FALSE;
        }
        else
        {
            pCallerAgent->AgentState=AS_Active;
        }
        //pCallerAgent->AgentState=AS_Active;
        //////////////////////////////////////////////////////////////////////////
        pCallerAgent->unlock();
        std::string sInfo = evt.EnAskMsg();

        CAgentOp op;
        op.SendResult(pCallerAgent,sInfo);
        pCallerAgent->lock();
        SetAgentStatusReportData(pCallerAgent);
        SetWSCDRData(pCallerAgent);
        pCallerAgent->unlock();
        pCallerAgent->Release();

    }
    else
    {
        m_pDlg->m_Log.Log("FLOW_LOG->%s 没有匹配的主叫",szChan1);
    }

    CAgent* pCalledAgent = m_pDlg->GetAgentFromChan(tChan2);
    if(pCalledAgent)
    {

        CEstablishEvt evt;
        evt.m_strAni = szCaller;
        evt.m_strDnis = szCalled;
        evt.m_strUCID = szUCID;
        CUntiTool tool;
        evt.m_strTime = tool.GetCurrTime();
        evt.m_strStation = pCalledAgent->sStation;
        evt.m_strAgentId = pCalledAgent->sWorkNo;
        pCalledAgent->lock();
        pCalledAgent->sOtherID = szChan1;
        //////////////////////////////////////////////////////////////////////////
        //2013-03-18 add by chenlin
        //增加咨询状态给报表，深圳电信需求
        if (pCalledAgent->bConsult)
        {
            //说明是咨询通话，表示为咨询中状态
            pCalledAgent->AgentState = AS_Consulting;
            pCalledAgent->bConsult = FALSE;
        }
        else
        {
            pCalledAgent->AgentState = AS_Active;
        }
        //pCalledAgent->AgentState=AS_Active;//19
        //////////////////////////////////////////////////////////////////////////

        pCalledAgent->unlock();
        std::string sInfo = evt.EnAskMsg();

        CAgentOp op;
        op.SendResult(pCalledAgent,sInfo);
        //SetWSCDRData(pCalledAgent);
        //19
        SetAgentStatusReportData(pCalledAgent);
        SetWSCDRData(pCalledAgent);
        pCalledAgent->Release();

    }
    else
    {
        m_pDlg->m_Log.Log("FLOW_LOG->%s 没有匹配的被叫",szChan2);
    }

    //m_pDlg->SetUnLock();

    return TRUE;
}

BOOL CHandleAskPBX::HandleConfEvt(const std::string &strEvt)
{
    BOOL bRet = FALSE;
    CConfEvt evt;
    evt.ParaResponseMsg(strEvt);

    evt.m_strRet = "Succ";
    evt.m_strUserData = "Conference";

//	CAgent* pAgent = m_pDlg->GetAgentFromStation(evt.m_strStation);
    //m_pDlg->SetLock();
    CAgent* pAgent = m_pDlg->GetAgentFromChan(evt.m_strChan);
    if(pAgent)
    {
        CUntiTool tool;
        evt.m_strTime = tool.GetCurrTime();
        evt.m_strStation = pAgent->sStation;
        evt.m_strAgentId = pAgent->sWorkNo;
        std::string strInfo = evt.EnSoftPhoneMsg();
        CAgentOp op;
        op.SendResult(pAgent,strInfo);
        pAgent->lock();
        pAgent->AgentState = AS_Conferencing;
        SetAgentStatusReportData(pAgent);
        pAgent->sHoldID = "";
	pAgent->sHoldStation="";
	pAgent->sHoldS.sHoldID = "";
	pAgent->sHoldS.sAni = "";
	pAgent->sHoldS.sDnis = "";
        pAgent->unlock();
        bRet = TRUE;
        pAgent->Release();
    }
    //m_pDlg->SetUnLock();

    return bRet;
}

BOOL CHandleAskPBX::HandleLoginEvt(const std::string &strEvt)
{
    CLoginEvt evt;
    evt.ParaRetPBXEvt(strEvt);

    evt.m_strRet = "Succ";
    evt.m_strUserData = "login succ";

    //m_pDlg->SetLock();
    CAgent* pAgent = m_pDlg->GetAgentFromStation(evt.m_strStation);
    if(pAgent)
    {
        CUntiTool tool;
        evt.m_strTime = tool.GetCurrTime();
        evt.m_strStation = pAgent->sStation;
        evt.m_strAgentId = pAgent->sWorkNo;
        std::string strInfo = evt.EnSoftPhoneMsg();
        CAgentOp op;
        BOOL bRet = op.SendResult(pAgent,strInfo);
        if(bRet)
        {
            pAgent->lock();
            pAgent->AgentState = AS_Login;
            SetAgentStatusReportData(pAgent);
            pAgent->bLogin = TRUE;
            pAgent->AgentState = AS_Notready;
            pAgent->sReasonCode = "0";
            pAgent->unlock();
            SetAgentStatusReportData(pAgent);

            CBusyEvt evt;
            evt.m_strRet = "Succ";
            evt.m_strUserData  = "After Login set agent busy ";
            m_pDlg->SetWorkNoToMap(pAgent);

        }
        pAgent->Release();
    }
    //m_pDlg->SetUnLock();
    return TRUE;
}

BOOL CHandleAskPBX::HandleLogoutEvt(const std::string &strEvt)
{
    BOOL bRet=FALSE;
    CLogoutEvt evt;
    evt.ParaRetPBXEvt(strEvt);

    evt.m_strRet = "Succ";
    evt.m_strUserData = "logout succ";

    //m_pDlg->SetLock();
    CAgent* pAgent = m_pDlg->GetAgentFromStation(evt.m_strStation);
    if(pAgent)
    {
        //判断是否所有技能组签出
        //	if(Islogoutallqueue(pAgent)) //true完全签出，执行以下逻
        {
            CUntiTool tool;
            evt.m_strTime = tool.GetCurrTime();
            evt.m_strStation = pAgent->sStation;
            evt.m_strAgentId = pAgent->sWorkNo;
            std::string strInfo = evt.EnSoftPhoneMsg();
            if(strInfo != "")
            {
                CAgentOp op;
                bRet = FALSE;
                if(pAgent->sAction == "Logout")
                {
                    m_pDlg->m_Log.Log("FLOW_LOG->坐席 %s Logout, Action=%s",pAgent->sStation.c_str(), pAgent->sAction.c_str());
                    bRet = op.SendResult(pAgent,strInfo);
                }
                else
                {
                    m_pDlg->m_Log.Log("FLOW_LOG->坐席 %s  Action=%s",pAgent->sStation.c_str(), pAgent->sAction.c_str());
                }
                pAgent->lock();
                pAgent->bLogin = FALSE;
                pAgent->AgentState = AS_Notlogin;
                SetAgentStatusReportData(pAgent);
                SetAgentStatusReportData(pAgent);
                pAgent->unlock();
                //new: delete操作
                m_pDlg->RemoveAgentFromMap(pAgent);

                //delete pAgent;//add by nyx
                //pAgent=NULL;//add by nyx

                bRet = TRUE;
            }
            pAgent->Release();
        }
    }
    //m_pDlg->SetUnLock();
    return bRet;
}

BOOL CHandleAskPBX::HandleQueueStaticEvt(const std::string &sEvt)
{
    CQueueEvt evt;
    evt.ParaQueueParams(sEvt);

    std::string sTenantID="0";//根据技能组获取该技能组所在的租户ID
    //根据evt从PBX获取的技能组号码，通过链表获取该技能组所对应的租户
    //POSITION pos = m_pDlg->m_PBXInfoList.GetHeadPosition();
    m_pDlg->m_PBXInfoCritical.acquire();
    std::list<PBXInfo*>::iterator pos = m_pDlg->m_PBXInfoList.begin();
    //auto pos = m_pDlg->m_PBXInfoList.begin();
    while (pos != m_pDlg->m_PBXInfoList.end())
    {
        //PBXInfo* pPBXInfo1 = m_pDlg->m_PBXInfoList.GetNext(pos);
        PBXInfo* pPBXInfo1 = *pos;

        if (pPBXInfo1->ssipSkillName == evt.sQueue)
        {
            sTenantID = pPBXInfo1->stenantId;
            break;
        }

        ++pos;
    }
    m_pDlg->m_PBXInfoCritical.release();

    //QueryQueue替换掉
    SetWSSkillSnap(&evt,sTenantID);
    return TRUE;
}

BOOL CHandleAskPBX::HandleJoinEvt(const std::string &sEvt)
{
    CGeneralUtils myGeneralUtil;

    char szQueueName[64];
    memset(szQueueName,0,64);
    myGeneralUtil.GetStringValue(sEvt,"Queue",szQueueName);

    char szChan[64];
    memset(szChan,0,64);
    myGeneralUtil.GetStringValue(sEvt,"Channel",szChan);

    char szCaller[64];
    memset(szCaller,0,64);
    myGeneralUtil.GetStringValue(sEvt,"Caller",szCaller);

    /************************************************************************/
    /*add by chenlin                                                        */
    char szUCID[128];
    memset(szUCID,0,128);
    myGeneralUtil.GetStringValue(sEvt,"Ucid",szUCID);

    char szTeantId[128];
    memset(szTeantId,0,128);
    myGeneralUtil.GetStringValue(sEvt,"TenantId",szTeantId);

    char szaccessNumber[256];
    memset(szaccessNumber,0,sizeof(szaccessNumber));
    myGeneralUtil.GetStringValue(sEvt,"AccessNumber",szaccessNumber);

    char szCallInType[32];
    memset(szCallInType,0,32);
    myGeneralUtil.GetStringValue(sEvt,"Direction",szCallInType);
    /************************************************************************/
    //说明坐席本身进入咨询技能组
    m_pDlg->m_Log.Log("TEST_LOG->join szChan=%s;szQueueName=%s",szChan,szQueueName);

    //m_pDlg->SetLock();
    CAgent* pAgent = m_pDlg->GetAgentFromChan(szChan);
    if(pAgent)
    {
        //将通话中送给状态送给坐席

        if(pAgent->sAction == "Consult")
        {
            CEstablishEvt evt;
            evt.m_strAni = szCaller;
            evt.m_strDnis = "";
            evt.m_strUCID = "";
            CUntiTool tool;
            evt.m_strTime = tool.GetCurrTime();
            evt.m_strStation = pAgent->sStation;
            evt.m_strAgentId = pAgent->sWorkNo;
            pAgent->lock();
            pAgent->AgentState=AS_Active;
            pAgent->unlock();
            std::string sInfo = evt.EnAskMsg();

            CAgentOp op;
            op.SendResult(pAgent,sInfo);
        }
        else
        {
            /************************************************************************/
            /*add by chenlin 不管接不接,存入技能组号，技能组详单和cdr报表需要       */
            //咨询技能足要除外
            pAgent->lock();
            pAgent->sQueueName = szQueueName;
            pAgent->unlock();
            /************************************************************************/
        }
        pAgent->Release();

    }
    //m_pDlg->SetUnLock();


    CUntiTool tool;


    CQueueCallInfo* pQueueCallInfo = new CQueueCallInfo;
    pQueueCallInfo->sAni = szCaller;
    pQueueCallInfo->sJoinTime = tool.GetCurrTime();
    pQueueCallInfo->sQueueName = szQueueName;
    /************************************************************************/
    /*                                                                      */
    pQueueCallInfo->sUcid = szUCID;
    pQueueCallInfo->saccessNumber = szaccessNumber;
    pQueueCallInfo->sCallType = szCallInType;
    pQueueCallInfo->sTeantId = szTeantId;
    pQueueCallInfo->sStation = "nunu";
    pQueueCallInfo->sWorkNo = "nunu";
    /************************************************************************/

    m_pDlg->m_MapQueueCallLock.acquire();
    //m_pDlg->m_MapQueueCall.SetAt(szChan,(CObject*&)pQueueCallInfo);
    m_pDlg->m_MapQueueCall[szChan] = pQueueCallInfo;
    m_pDlg->m_MapQueueCallLock.release();

    return TRUE;
}

BOOL CHandleAskPBX::HandleLeaveEvt(const std::string &sEvt)
{
    CGeneralUtils myGeneralUtil;

    char szChan[64];
    memset(szChan,0,64);
    myGeneralUtil.GetStringValue(sEvt,"Channel",szChan);

    //出队列事件Leave增加字段Reason表示排队结果（answer/noanswer）
    char szReason[64];
    memset(szReason,0,64);
    myGeneralUtil.GetStringValue(sEvt,"Reason",szReason);

    m_pDlg->m_MapQueueCallLock.acquire();
    CQueueCallInfo* pQueueCall = NULL;
    //BOOL bret=m_pDlg->m_MapQueueCall.Lookup(szChan,(CObject*&)pQueueCall);
    //auto bret = m_pDlg->m_MapQueueCall.find(szChan);
    std::map<std::string, CQueueCallInfo*>::iterator bret = m_pDlg->m_MapQueueCall.find(szChan);
    if(bret!=m_pDlg->m_MapQueueCall.end())
        pQueueCall = bret->second;
    //if(bret!=m_pDlg->m_MapQueueCall.end() && pQueueCall)
    if(pQueueCall)
    {
        CUntiTool tool;
        std::string endTime = tool.GetCurrTime();
        pQueueCall->sLeaveTime = endTime;
        //设置技能组的详细信息
        std::string sCmd = "skillRecord|";
        std::string sData = m_pDlg->m_strskillRecordWeb;
        REPLACE(sData, "[tenantId]",pQueueCall->sTeantId);
        REPLACE(sData, "[skill]",pQueueCall->sQueueName);
        REPLACE(sData, "[callType]",pQueueCall->sCallType);
        REPLACE(sData, "[caller]",pQueueCall->sAni);
        REPLACE(sData, "[inQueueTime]",pQueueCall->sJoinTime);
        REPLACE(sData, "[outQueueTime]",tool.GetCurrTime());
        REPLACE(sData, "[queueResult]",szReason);
        //----------------2013-03-01-------------------------
        //----------add jyf----------------------------------
        //2013-03-06 copy by chenlin
        /*********************************************************/
        //if (strcmp(szReason, "answer") == 0)
        //{
        //edit by chenlin
// 		if (pAgent)
        m_pDlg->m_Log.Log("TEST_LOG->离开队列:szChan=%s",szChan);
// 			CAgent *pAgent = m_pDlg->GetAgentFromChan(szChan);

        REPLACE(sData, "[agentExtension]", pQueueCall->sStation);
        REPLACE(sData, "[agentNo]", pQueueCall->sWorkNo);
// 			}
// 			else
// 			{
// 				sData.Replace("[agentExtension]", "nu");
// 				sData.Replace("[agentNo]", "nu");
// 			}
        //}else
        //{
        //	sData.Replace("[agentExtension]", "");
        //	sData.Replace("[agentNo]", "");
        //}
        //---------------------------end--------------------------
        REPLACE(sData, "[ucid]", pQueueCall->sUcid);
        if(pQueueCall->saccessNumber != "")
        {
            REPLACE(sData, "[accessNumber]", pQueueCall->saccessNumber);
        }
        else
        {
            REPLACE(sData, "[accessNumber]", "^");
        }
        //m_pDlg->m_MapQueueCall.RemoveKey(szChan);
        //auto itdel = m_pDlg->m_MapQueueCall.find(szChan);
        std::map<std::string, CQueueCallInfo*>::iterator itdel = m_pDlg->m_MapQueueCall.find(szChan);
        if(itdel!=m_pDlg->m_MapQueueCall.end())
            m_pDlg->m_MapQueueCall.erase(itdel);
        delete pQueueCall;
        pQueueCall = NULL;

        std::string sMsg = sCmd + sData;
        //sMsg += sCmd;
        //sMsg += sData;


        m_pDlg->m_WSQueue.Push(sMsg);
        //SetEvent(m_pDlg->m_HandleWS.m_hStartHandleWebResponse);
        m_pDlg->m_HandleWS.m_hStartHandleWebResponse.release();

    }
    else
    {
        m_pDlg->m_Log.Log("leave事件找不到 channel %s", szChan);
    }
    m_pDlg->m_MapQueueCallLock.release();

    return TRUE;
}

BOOL CHandleAskPBX::HandleUserEvent(const std::string &sEvt)
{
    CGeneralUtils myGeneralUtil;

    char szUserEvent[32];
    memset(szUserEvent,0,32);
    myGeneralUtil.GetStringValue(sEvt,"UserEvent",szUserEvent);

    char szChan[128];
    memset(szChan,0,128);
    myGeneralUtil.GetStringValue(sEvt,"Channel",szChan);



    m_pDlg->m_Log.Log("FLOW_LOG->userEvent=%s",szUserEvent);

    if(strcmp(szUserEvent,"customQueueInfo") == 0)
    {
        //呼入队列时产生
        char szUCID[128];
        memset(szUCID,0,128);
        myGeneralUtil.GetStringValue(sEvt,"Ucid",szUCID);

        char szQueueName[512];
        memset(szQueueName,0,512);
        myGeneralUtil.GetStringValue(sEvt,"Name",szQueueName);

        char szTeantId[128];
        memset(szTeantId,0,128);
        myGeneralUtil.GetStringValue(sEvt,"TenantId",szTeantId);

        char szaccessNumber[256];
        memset(szaccessNumber,0,sizeof(szaccessNumber));
        myGeneralUtil.GetStringValue(sEvt,"AccessNumber",szaccessNumber);
        /************************************************************************/
        //2013-03-06 add by chenlin
        //为了存入呼入的技能组号
        char szCalled[128];
        memset(szCalled, 0, 128);
        myGeneralUtil.GetStringValue(sEvt, "Called", szCalled);
        /************************************************************************/

        CQueueCallInfo* pQueueCallInfo = NULL;
        m_pDlg->m_MapQueueCallLock.acquire();
        //BOOL bret=m_pDlg->m_MapQueueCall.Lookup(szChan,(CObject*&)pQueueCallInfo);
        //auto bret = m_pDlg->m_MapQueueCall.find(szChan);
        std::map<std::string, CQueueCallInfo*>::iterator bret = m_pDlg->m_MapQueueCall.find(szChan);
        if(bret!=m_pDlg->m_MapQueueCall.end())
            pQueueCallInfo = bret->second;
        //if(bret&&pQueueCallInfo)
        if(pQueueCallInfo)
        {
            char szCallInType[32];
            memset(szCallInType,0,32);
            myGeneralUtil.GetStringValue(sEvt,"Direction",szCallInType);

            CUntiTool tool;
            pQueueCallInfo->sUcid = szUCID;
            pQueueCallInfo->sQueueName = szQueueName;
            pQueueCallInfo->sTeantId = szTeantId;
            pQueueCallInfo->sCallType = szCallInType;
            pQueueCallInfo->saccessNumber = szaccessNumber;

            /************************************************************************/
            //2013-03-06 add by chenlin
            //为了存入呼入的技能组号
            //m_pDlg->SetLock();
            CAgent *pAgent = m_pDlg->GetAgentFromStation(szCalled);
            if (pAgent)
            {
                pAgent->lock();
                pAgent->sQueueName = szQueueName;
                pQueueCallInfo->sStation = pAgent->sStation;
                pQueueCallInfo->sWorkNo = pAgent->sWorkNo;
                pAgent->unlock();
                pAgent->Release();
            }
            //m_pDlg->SetUnLock();
            /************************************************************************/

            m_pDlg->m_Log.Log("FLOW_LOG->UserEvent:customQueueInfo,Chan=%s,Ucid=%s,QueueName=%s,TeantID=%s,CallType=%s,accessNumber=%s",szChan,szUCID,szQueueName,szTeantId,szCallInType,szaccessNumber);
        }
        else
        {
            m_pDlg->m_Log.Log("FLOW_LOG->UserEvent:customQueueInfo no find chan=%s QueueCallInfo.",szChan);
        }
        m_pDlg->m_MapQueueCallLock.release();
    }
    //
    else if(strcmp(szUserEvent,"customRecordInfo") == 0)
    {
        //录音事件：拨号时产生(不需要通话)；座席通话后产生
        //Event=UserEvent;Privilege=user,all;UserEvent=customRecordInfo;Direction=out;Caller=21008;Reason=caller;TenantId=tenant2e629aafbfe042fb8296b1f677f96143;ActionID=linux-00000001;Ucid=192-168-20-91-2011-04-14-17-22-43-1302816162-484;Action=UserEvent;FileName=/var/spool/asterisk/monitor/tenant2e629aafbfe042fb8296b1f677f96143/2011/04/14/tenant2e629aafbfe042fb8296b1f677f96143-20110414172252-21008-53228.wav;Called=6111119
        //Event=UserEvent;Privilege=user,all;UserEvent=customRecordInfo;Direction=in;Caller=6111119;Reason=caller;TenantId=tenant2e629aafbfe042fb8296b1f677f96143;ActionID=linux-00000001;Ucid=192-168-20-91-2011-04-14-17-25-26-1302816326-486;Action=UserEvent;FileName=/var/spool/asterisk/monitor/tenant2e629aafbfe042fb8296b1f677f96143/2011/04/14/tenant2e629aafbfe042fb8296b1f677f96143-20110414172527-6111119-63577.wav;Called=21008

        char szFileName[512];
        memset(szFileName,0,512);
        myGeneralUtil.GetStringValue(sEvt,"FileName",szFileName);

        char szUcid[512];
        memset(szUcid,0,512);
        myGeneralUtil.GetStringValue(sEvt,"Ucid",szUcid);

        char szCaller[64];
        memset(szCaller,0,64);
        myGeneralUtil.GetStringValue(sEvt,"Caller",szCaller);

        char szCalled[64];
        memset(szCalled,0,64);
        myGeneralUtil.GetStringValue(sEvt,"Called",szCalled);

        char szCallDirection[64];
        memset(szCallDirection,0,64);
        myGeneralUtil.GetStringValue(sEvt,"Direction",szCallDirection);


        char szcallerAgentId[64];
        memset(szcallerAgentId,0,64);
        myGeneralUtil.GetStringValue(sEvt,"callerAgentId",szcallerAgentId);

        char szcalledAgentId[64];
        memset(szcalledAgentId,0,64);
        myGeneralUtil.GetStringValue(sEvt,"calledAgentId",szcalledAgentId);


        char szReason[64];
        memset(szReason,0,64);
        myGeneralUtil.GetStringValue(sEvt,"Reason",szReason);

        std::string strReason = szReason;
        CAgent *pAgent = NULL;
        if (strReason == "caller")//给主叫的
        {
            pAgent = m_pDlg->GetAgentFromStation(szCaller);
            if (pAgent)
            {
                m_pDlg->m_Log.Log("FLOW_LOG->caller agent source: %s", pAgent->m_strSource.c_str());
                m_pDlg->m_Log.Log("FLOW_LOG->caller agent station: %s", pAgent->sStation.c_str());
                m_pDlg->m_Log.Log("FLOW_LOG->caller agent WorkNo: %s", pAgent->sWorkNo.c_str());
                m_pDlg->m_Log.Log("FLOW_LOG->record ucid: %s", szUcid);
                m_pDlg->m_Log.Log("FLOW_LOG->record RecordID: %s", szFileName);
                CRecordEvt evt;
                //edit by chenlin 协议修改，同时传UCID和filename
                //暂时应用在http客户端，下次升级统一应用到全体
                if (pAgent->m_strSource == "C_WEB" || pAgent->m_strSource == "S_WEB")
                {
                    m_pDlg->m_Log.Log("FLOW_LOG->agent is S_WEB: %s", szCaller);
                    evt.m_strUCID = szUcid;
                    evt.m_strRecordID = szFileName;
                }
                else
                {
                    evt.m_strUCID = szFileName;
                }
                //	evt.m_strUCID = szFileName;//将录音文件名传给核心，原来是传ucid作为录音标识。2011-4-27 by ly
                CUntiTool tool;
                evt.m_strTime = tool.GetCurrTime();
                evt.m_strStation = pAgent->sStation;
                evt.m_strAgentId = pAgent->sWorkNo;

                //存入录音recordID以备GetPopData获取
                pAgent->lock();
                pAgent->m_recordID = szFileName;
                pAgent->unlock();

                std::string sMsg = evt.EnSoftPhoneMsg();
                m_pDlg->m_IOOpt.SendMsgToUser(pAgent->sock,sMsg);
                pAgent->Release();

            }
        }
        else if(strReason == "called")//给被叫的
        {
            pAgent = m_pDlg->GetAgentFromStation(szCalled);
            if (pAgent)
            {
                m_pDlg->m_Log.Log("FLOW_LOG->called agent source: %s", pAgent->m_strSource.c_str());
                m_pDlg->m_Log.Log("FLOW_LOG->called agent station: %s", pAgent->sStation.c_str());
                m_pDlg->m_Log.Log("FLOW_LOG->called agent WorkNo: %s", pAgent->sWorkNo.c_str());
                m_pDlg->m_Log.Log("FLOW_LOG->record ucid: %s", szUcid);
                m_pDlg->m_Log.Log("FLOW_LOG->record RecordID: %s", szFileName);
                CRecordEvt evt;
                //edit by chenlin 协议修改，同时传UCID和filename
                //暂时应用在http客户端，下次升级统一应用到全体
                if (pAgent->m_strSource == "C_WEB" || pAgent->m_strSource == "S_WEB")
                {
                    m_pDlg->m_Log.Log("FLOW_LOG->agent is S_WEB: %s", szCalled);
                    evt.m_strUCID = szUcid;
                    evt.m_strRecordID = szFileName;
                }
                else
                {
                    evt.m_strUCID = szFileName;
                }
                //evt.m_strUCID = szFileName;//将录音文件名传给核心，原来是传ucid作为录音标识。2011-4-27 by ly
                CUntiTool tool;
                evt.m_strTime = tool.GetCurrTime();
                evt.m_strStation = pAgent->sStation;
                evt.m_strAgentId = pAgent->sWorkNo;

                //存入录音recordID以备GetPopData获取
                pAgent->lock();
                pAgent->m_recordID = szFileName;
                pAgent->unlock();

                std::string sMsg = evt.EnSoftPhoneMsg();
                m_pDlg->m_IOOpt.SendMsgToUser(pAgent->sock,sMsg);
                pAgent->Release();

            }
        }


//        std::string strDirection;
//        strDirection = szCallDirection;
//        CAgent* pAgent = NULL;
//
//        if (strDirection == "in")
//        {
//            //m_pDlg->SetLock();
//            pAgent = m_pDlg->GetAgentFromStation(szCalled);
//            if(pAgent)
//            {
//                CRecordEvt evt;
//                evt.sUCID = szFileName;//将录音文件名传给核心，原来是传ucid作为录音标识。2011-4-27 by ly
//                //////////////////////////////////////////////////////////////////////////
//                //2013-01-07 add by chenlin ↓
//                //////////////////////////////////////////////////////////////////////////
//                //存入录音recordID以备GetPopData获取
//                pAgent->lock();
//                pAgent->m_recordID = szFileName;
//                pAgent->unlock();
//                //////////////////////////////////////////////////////////////////////////
//                //2013-01-07 add by chenlin ↑
//                //////////////////////////////////////////////////////////////////////////
//                std::string sMsg = evt.EnSoftPhoneMsg();
//                m_pDlg->m_IOOpt.SendMsgToUser(pAgent->sock,sMsg);
//                pAgent->Release();
//
//            }
//            //m_pDlg->SetUnLock();
//
//        }
//        else if (strDirection == "out")
//        {
//            //m_pDlg->SetLock();
//            pAgent = m_pDlg->GetAgentFromStation(szCaller);
//            if(pAgent)
//            {
//                CRecordEvt evt;
//                evt.sUCID = szFileName;//将录音文件名传给核心，原来是传ucid作为录音标识。2011-4-27 by ly
//                //////////////////////////////////////////////////////////////////////////
//                //2013-01-07 add by chenlin ↓
//                //////////////////////////////////////////////////////////////////////////
//                //存入录音recordID以备GetPopData获取
//                pAgent->lock();
//                pAgent->m_recordID = szFileName;
//                pAgent->unlock();
//                //////////////////////////////////////////////////////////////////////////
//                //2013-01-07 add by chenlin ↑
//                //////////////////////////////////////////////////////////////////////////
//                std::string sMsg = evt.EnSoftPhoneMsg();
//                m_pDlg->m_IOOpt.SendMsgToUser(pAgent->sock,sMsg);
//                pAgent->Release();
//            }
//            //m_pDlg->SetUnLock();
//        }
//        else if (strDirection == "internal")
//        {
//            //m_pDlg->SetLock();
//            CAgent *pAgent1 = m_pDlg->GetAgentFromStation(szCaller);
//            CAgent *pAgent2 = m_pDlg->GetAgentFromStation(szCalled);
//            if (pAgent1)
//            {
//                CRecordEvt evt;
//                evt.sUCID = szFileName;//将录音文件名传给核心，原来是传ucid作为录音标识。2011-4-27 by ly
//                //////////////////////////////////////////////////////////////////////////
//                //2013-01-07 add by chenlin ↓
//                //////////////////////////////////////////////////////////////////////////
//                //存入录音recordID以备GetPopData获取
//                pAgent1->lock();
//                pAgent1->m_recordID = szFileName;
//                pAgent1->unlock();
//                //////////////////////////////////////////////////////////////////////////
//                //2013-01-07 add by chenlin ↑
//                //////////////////////////////////////////////////////////////////////////
//                std::string sMsg = evt.EnSoftPhoneMsg();
//                m_pDlg->m_IOOpt.SendMsgToUser(pAgent1->sock,sMsg);
//                pAgent1->Release();
//            }
//            if (pAgent2)
//            {
//                CRecordEvt evt;
//                evt.sUCID = szFileName;//将录音文件名传给核心，原来是传ucid作为录音标识。2011-4-27 by ly
//                //////////////////////////////////////////////////////////////////////////
//                //2013-01-07 add by chenlin ↓
//                //////////////////////////////////////////////////////////////////////////
//                //存入录音recordID以备GetPopData获取
//                pAgent2->lock();
//                pAgent2->m_recordID = szFileName;
//                pAgent2->unlock();
//                //////////////////////////////////////////////////////////////////////////
//                //2013-01-07 add by chenlin ↑
//                //////////////////////////////////////////////////////////////////////////
//                std::string sMsg = evt.EnSoftPhoneMsg();
//                m_pDlg->m_IOOpt.SendMsgToUser(pAgent2->sock,sMsg);
//                pAgent2->Release();
//            }
//            //m_pDlg->SetUnLock();
//        }
    }
    else if(strcmp(szUserEvent,"customSpyInfo") == 0)
    {
        //通知外部程序监听：监听成功事件
        /*
        Event=UserEvent;
        Privilege=user,all;
        UserEvent=customSpyInfo;
        Channel=SIP/21009-0000007e;
        Caller=21009;
        Called=21008
        */
        //Event=UserEvent;
        //Privilege=user,all;
        //UserEvent=customSpyInfo;
        //Caller=56769000;
        //Called=s;
        //Channel=SIP/20.0.1.5-000001aa

        char szCaller[32];
        memset(szCaller,0,32);
        myGeneralUtil.GetStringValue(sEvt,"Caller",szCaller);


        char szCalled[32];
        memset(szCalled,0,32);
        myGeneralUtil.GetStringValue(sEvt,"Called",szCalled);


        char szChan[64];
        memset(szChan,0,64);
        myGeneralUtil.GetStringValue(sEvt,"Channel",szChan);

        char szUcid[128];
        memset(szUcid,0,128);
        myGeneralUtil.GetStringValue(sEvt,"Ucid",szUcid);

        char szTenantId[128];
        memset(szTenantId,0,128);
        myGeneralUtil.GetStringValue(sEvt,"TenantId",szTenantId);

        char szDirection[128];
        memset(szDirection,0,128);
        myGeneralUtil.GetStringValue(sEvt,"Direction",szDirection);


        //在SPY-info中绑定
        //m_pDlg->SetLock();
        CAgent* pAgent = m_pDlg->GetAgentFromStation(szCaller);
        if(pAgent)
        {
            //pAgent->sSelfChanID = szChan;

            if(pAgent->sAction == "Monitor")
            {
                CMonitorEvt evt;
                evt.m_strRet = "Succ";
                evt.m_strStation = szCaller;
                evt.m_strMonitorNo = szCalled;
                CUntiTool tool;
                evt.m_strTime = tool.GetCurrTime();
                evt.m_strStation = pAgent->sStation;
                evt.m_strAgentId = pAgent->sWorkNo;
                evt.m_strUCID = szUcid;
                std::string sMsg = evt.EnSoftPhoneMsg();
                CAgentOp op;

                //
                pAgent->lock();
                pAgent->sSelfChanID = szChan;
                pAgent->orginAni = szCaller;
                pAgent->orginDnis = szCalled;
                pAgent->originUCID = szUcid;
                pAgent->orginCallDirect = szDirection;
                pAgent->AgentState = AS_Monitor;
                SetWSCDRData(pAgent);
                SetAgentStatusReportData(pAgent);
                pAgent->unlock();

                //m_pDlg->m_MapChanToAgent.SetAt(szChan,(CObject*&)pAgent);
                //m_pDlg->m_MapChanToAgent[szChan] = pAgent;
                m_pDlg->SetAgentToChan(szChan,pAgent);


                op.SendResult(pAgent,sMsg);


            }
            else if(pAgent->sAction == "Observer")
            {
                CObserverEvt evt;
                evt.m_strRet = "Succ";
                evt.m_strStation = szCaller;
                evt.m_strObserverNo = szCalled;
                CUntiTool tool;
                evt.m_strTime = tool.GetCurrTime();
                evt.m_strStation = pAgent->sStation;
                evt.m_strAgentId = pAgent->sWorkNo;
                evt.m_strUCID = szUcid;
                //cdr    信息
                pAgent->lock();
                pAgent->sSelfChanID = szChan;
                pAgent->orginAni = szCaller;
                pAgent->orginDnis = szCalled;
                pAgent->originUCID = szUcid;
                pAgent->orginCallDirect = szDirection;
                pAgent->AgentState  = AS_Observe;
                SetWSCDRData(pAgent);
                SetAgentStatusReportData(pAgent);
                pAgent->unlock();
                //m_pDlg->m_MapChanToAgent.SetAt(szChan,(CObject*&)pAgent);
                //m_pDlg->m_MapChanToAgent[szChan] = pAgent;
                m_pDlg->SetAgentToChan(szCalled,pAgent);


                std::string sMsg = evt.EnSoftPhoneMsg();
                CAgentOp op;
                op.SendResult(pAgent,sMsg);
            }
            pAgent->Release();
        }
        //m_pDlg->SetUnLock();
    }
    else if(strcmp(szUserEvent,"customMusicOnHold") == 0)
    {
        //通过AMI命令显示进入保持状态时的事件：保持成功事件
        char szChan[64];
        memset(szChan,0,64);
        myGeneralUtil.GetStringValue(sEvt,"Channel",szChan);

        char szDisplay[128];
        memset(szDisplay,0,128);
        myGeneralUtil.GetStringValue(sEvt,"Display",szDisplay);

        CAgent* pAgent = m_pDlg->GetAgentFromChan(szChan);
        if(pAgent && strcmp(szDisplay,"1")==0)
        {
            CHoldEvt evt;
            evt.m_strRet = "Succ";
            evt.m_strUserData = "Hold Succ";
            CUntiTool tool;
            evt.m_strTime = tool.GetCurrTime();
            evt.m_strStation = pAgent->sStation;
            evt.m_strAgentId = pAgent->sWorkNo;
            std::string strInfo = evt.EnSoftPhoneMsg();
            CAgentOp op;
            pAgent->lock();
            op.SendResult(pAgent,strInfo);
            pAgent->AgentState = AS_Hold;
            pAgent->sHoldID = pAgent->sOtherID;
	    pAgent->sHoldS.sHoldID = pAgent->sOtherID;
	    pAgent->sHoldS.sAni = pAgent->sAni;
	    pAgent->sHoldS.sDnis = pAgent->sDnis;

            SetAgentStatusReportData(pAgent);
            pAgent->unlock();
            pAgent->Release();

        }
        //////////////////////////////////////////////////////////////////////////
        //2013-03-06 edit by chenlin
        //else if(pAgent && strlen(szDisplay) > 3)
        else if(strlen(szDisplay) > 3)
        {
            CUntiTool tool;
            std::string sAction;
            std::string sStation;
            std::string sVal;

            tool.GetAgentInfoFromHoldVal(szDisplay,sStation,sAction,sVal);
            //m_pDlg->SetLock();
            CAgent* pAgent = m_pDlg->GetAgentFromStation(sStation);

            if(sVal == "0" && pAgent)
            {
                pAgent->lock();
                m_pDlg->m_Log.Log("ERROR_LOG->Station = %s , Action = %s",sStation.c_str(), sAction.c_str());

                if(sAction == "ConsultCancel")
                {
                    pAgent->ConsultCancel_1();
                }
                else if(sAction == "ConsultTrans")
                {
                    pAgent->ConsultTrans_1();
                }
                else if(sAction == "Conference")
                {
                    pAgent->Confercece_1();
                }
                pAgent->unlock();
                pAgent->Release();
            }

            //m_pDlg->SetUnLock();

        }
    }
    //////////////////////////////////////////////////////////////////////////
    //2013-01-29 add by chenlin ↓
    //新增拦截事件
    //////////////////////////////////////////////////////////////////////////
    else if (strcmp(szUserEvent,"customInterceptInfo") == 0)
    {
        char szCalled[32];
        memset(szCalled,0,32);
        myGeneralUtil.GetStringValue(sEvt,"Called",szCalled);



        char szCaller[32];
        memset(szCaller,0,32);
        myGeneralUtil.GetStringValue(sEvt,"Caller",szCaller);



        char szChan[64];
        memset(szChan,0,64);
        myGeneralUtil.GetStringValue(sEvt,"Channel",szChan);

        char szUcid[128];
        memset(szUcid,0,128);
        myGeneralUtil.GetStringValue(sEvt,"Ucid",szUcid);

        char szTenantId[128];
        memset(szTenantId,0,128);
        myGeneralUtil.GetStringValue(sEvt,"TenantId",szTenantId);

        char szDirection[128];
        memset(szDirection,0,128);
        myGeneralUtil.GetStringValue(sEvt,"Direction",szDirection);

        //m_pDlg->SetLock();
        CAgent* pAgent = m_pDlg->GetAgentFromStation(szCaller);
        if(pAgent)
        {
            m_pDlg->m_Log.Log("TEST_LOG->CHAN=%s",szChan);
            pAgent->lock();
            pAgent->sAni = szCaller;
            pAgent->sDnis = szCalled;
            pAgent->AgentState = AS_HoldUp;
            pAgent->sSelfChanID = szChan;
            pAgent->sCallDirect = szDirection;
            pAgent->sUCID = szUcid;

            //m_pDlg->m_MapChanToAgent.SetAt(szChan,(CObject*&)pAgent);
            //m_pDlg->m_MapChanToAgent[szChan] = pAgent;
            m_pDlg->SetAgentToChan(szChan, pAgent);

            SetAgentStatusReportData(pAgent);
            SetWSCDRData(pAgent);
            pAgent->unlock();
            CMakeCallEvt evt;
            evt.m_strRet = "Succ";
            evt.m_strAni = szCaller;
            evt.m_strDnis = szCalled;
            evt.m_strUCID = szUcid;
            evt.m_strDirector = szDirection;
            CUntiTool tool;
            evt.m_strTime = tool.GetCurrTime();
            evt.m_strStation = pAgent->sStation;
            evt.m_strAgentId = pAgent->sWorkNo;

            std::string sMsg = evt.EnSoftPhoneMsg();
            CAgentOp op;
            op.SendResult(pAgent,sMsg);
            pAgent->Release();

        }
        //m_pDlg->SetUnLock();
    }
    else if(strcmp(szUserEvent,"IvrAgentLoginEvt") == 0)
    {
        char szbuffer[128];
        myGeneralUtil.GetStringValue(sEvt,"Ext",szbuffer);

        m_pDlg->SetAgentToStation(szbuffer);
        CAgent* pAgent = m_pDlg->GetAgentFromStation(szbuffer);
        if(pAgent)
        {
            pAgent->lock();
            pAgent->SetMainWnd(m_pDlg);
            pAgent->SetMainSocket(&m_pDlg->m_SockManager);
            pAgent->AgentinitState = IDLE_STATE;

            pAgent->AgentState = AS_Login;

            myGeneralUtil.GetStringValue(sEvt,"Type",szbuffer);
            pAgent->m_strSource = szbuffer;

            myGeneralUtil.GetStringValue(sEvt,"Skill",szbuffer);
            pAgent->m_tempSkill = szbuffer;

            myGeneralUtil.GetStringValue(sEvt,"AgentID",szbuffer);
            pAgent->sWorkNo = szbuffer;

            myGeneralUtil.GetStringValue(sEvt,"TeantId",szbuffer);
            pAgent->sTeantid = szbuffer;

            myGeneralUtil.GetStringValue(sEvt,"Ext",szbuffer);
            pAgent->sStation = szbuffer;

            pAgent->m_HangupStatus = AS_Idle;

            SetAgentStatusReportData(pAgent);
            m_pDlg->SetWorkNoToMap(pAgent);

            pAgent->unlock();
            pAgent->Release();

            m_pDlg->m_Log.Log("FLOW_LOG->IVR Login workno = %s; station = %s", pAgent->sWorkNo.c_str(), pAgent->sStation.c_str());
        }
    }
    //////////////////////////////////////////////////////////////////////////
    //2013-01-09 add by chenlin ↑
    //////////////////////////////////////////////////////////////////////////
    return TRUE;
}

BOOL CHandleAskPBX::HandleUCIDEvt(const std::string &sEvt)
{
    CGeneralUtils myGeneralUtil;

    char szActionID[128];
    memset(szActionID,0,128);
    myGeneralUtil.GetStringValue(sEvt,"ActionID",szActionID);

    char szVal[128];
    memset(szVal,0,128);
    myGeneralUtil.GetStringValue(sEvt,"Value",szVal);

    std::string sValName;
    std::string sStation;
    CUntiTool tool;
    tool.GetStationFromAction(szActionID,sValName,sStation);

    if(sValName == "ucid")
    {
        //返回录音事件
        //m_pDlg->SetLock();
        CAgent* pAgent = m_pDlg->GetAgentFromStation(sStation);
        if(pAgent)
        {
            CRecordEvt evt;
            evt.m_strStation = sStation;
            evt.m_strUCID = szVal;
            CUntiTool tool;
            evt.m_strTime = tool.GetCurrTime();
            evt.m_strAgentId = pAgent->sWorkNo;
            std::string sMsg = evt.EnSoftPhoneMsg();
            m_pDlg->m_IOOpt.SendMsgToUser(pAgent->sock,sMsg);
            pAgent->Release();
        }
        //m_pDlg->SetUnLock();
    }
    return TRUE;
}

BOOL CHandleAskPBX::HandleResponse(const std::string &strEvt)
{
    CResponseMsg msg;

    std::string strBody = msg.ParaAskPBXResponse(strEvt);

    //
    if(strBody != "")
    {

        //陈林修改优化逻辑性能，先去Agent对象，取出来再去做别的，
        CAgent* pAgent =  m_pDlg->GetAgentFromStation(msg.m_strStation);

        if(pAgent)
        {
            CUntiTool tool;
            msg.m_strTime = tool.GetCurrTime();
            msg.m_strAgentId = pAgent->sWorkNo;
            msg.m_pbxIP = m_pDlg->m_SettingData.m_strPBX;
            msg.m_tenantID = pAgent->sTeantid;
            strBody = msg.ParaAskPBXResponse(strEvt);

            if(msg.m_strOperation == "Login")
            {
                //目前签入成功

                if(msg.m_strRet == "Succ")
                {
                    //对于成功
                    m_pDlg->m_Log.Log("FLOW_LOG->Station %s %s skill %s Succ, Msg: %s",msg.m_strStation.c_str(), msg.m_strOperation.c_str(), msg.m_strQueue.c_str(), msg.m_strUserData.c_str());
                    //m_pDlg->m_IOOpt.SendMsgToUser(pAgent->sock,msg.m_strBody);
                    pAgent->lock();
                    pAgent->setSkillState(msg.m_strQueue,"T");
                    pAgent->unlock();
                }
                else if(msg.m_strRet == "Fail")
                {
                    //

                    if(msg.m_strUserData == "Unable to add interface=Already there")
                    {
                        //认为登录是成功的，转义一下！
                        m_pDlg->m_Log.Log("FLOW_LOG->Station %s %s Success, Msg: %s",msg.m_strStation.c_str(), msg.m_strOperation.c_str(), msg.m_strUserData.c_str());
                        REPLACE(msg.m_strBody, ">Fail<",">Succ<");
                        //m_pDlg->m_IOOpt.SendMsgToUser(pAgent->sock,msg.m_strBody);
                        pAgent->lock();
                        pAgent->setSkillState(msg.m_strQueue,"T");
                        pAgent->unlock();
                    }
                    else
                    {
                        pAgent->lock();
                        pAgent->setSkillState(msg.m_strQueue,"F");
                        pAgent->unlock();
                        m_pDlg->m_Log.Log("FLOW_LOG->Station %s %s Fail, Msg: %s",msg.m_strStation.c_str(), msg.m_strOperation.c_str(), msg.m_strUserData.c_str());
                        m_pDlg->m_IOOpt.SendMsgToUser(pAgent->sock,msg.m_strBody);
                        //如果全部登陆失败了，但是此时需要清空坐席端
                    }
                }

                int nNum = pAgent->IsFullSills();

                if(nNum == 0)
                {
                    //还存在未签入技能组
                }
                else if(nNum == 1)
                {
                    //存在失败的
                    pAgent->LogOut();
                    //					closesocket(pAgent->sock);
                }
                else if(nNum == 2)
                {
                    //全部成功了
                    msg.m_strQueue = pAgent->m_strAllSkill;
                    m_pDlg->m_IOOpt.SendMsgToUser(pAgent->sock,msg.EncodeResp());
                    //执行HandleLoginEvt的动作

                    pAgent->lock();
                    pAgent->AgentState = AS_Login;
                    SetAgentStatusReportData(pAgent);
                    pAgent->bLogin = TRUE;

                    if(pAgent->m_strSource == "C_WEB")
                    {
                        pAgent->SetIdle();
                    }
                    else
                    {
                        switch (pAgent->m_nDefStatus)
                        {
                        case AS_Idle:
                            pAgent->SetIdle();
                            break;
                        default:
                            pAgent->SetBusy();
                            break;
                        }
                    }

                    /*pAgent->AgentState = AS_Notready;
                    pAgent->sReasonCode = "0";
                    SetAgentStatusReportData(pAgent);*/
                    pAgent->unlock();

                    /*	CBusyEvt evt;
                    	evt.m_strRet = "Succ";
                    	evt.m_strUserData  = "After Login set agent busy ";*/
                    m_pDlg->SetWorkNoToMap(pAgent);

                    //
                }
                else
                {
                    //不可能发生
                }
            }
            else if(msg.m_strOperation == "Logout")
            {
                //不管成功还是失败，都给坐席端回签出成功的消息，在Ast平台上，签出失败的原因只有一个，分机不在技能组中。

                m_pDlg->m_Log.Log("FLOW_LOG->Station %s %s  from %s, Msg: %s",msg.m_strStation.c_str(), msg.m_strOperation.c_str(), msg.m_strQueue.c_str(), msg.m_strUserData.c_str());
                if(msg.m_strRet == "Succ")
                {
                    m_pDlg->m_IOOpt.SendMsgToUser(pAgent->sock,msg.m_strBody);
                }
                else if(msg.m_strRet == "Fail")
                {
                    //////////////////////////////////////////////////////////////////////////
                    // 2013-02-22 edit by chenlin
                    if(msg.m_strUserData == "Unable to remove interface=Not there")
                        //////////////////////////////////////////////////////////////////////////
                    {
                        //已经不在队列了，说明签出成功了！！但是不会产生LogoutEvt事件了，此时资源无法清空！
                        //但是仍然告诉坐席签出成功了.
                        REPLACE(msg.m_strBody, ">Fail<",">Succ<");
                        m_pDlg->m_IOOpt.SendMsgToUser(pAgent->sock,msg.m_strBody);
                    }
                    else
                    {
                        //致命错误，肯定失败！！或者根据本程序不可能发生的错误，万一发生了，也要返回错误。
                        m_pDlg->m_IOOpt.SendMsgToUser(pAgent->sock,msg.m_strBody);
                    }
                }

                //
                pAgent->lock();
                pAgent->setSkillState(msg.m_strQueue,"C");
                pAgent->unlock();
                int nNum = pAgent->IsOutSkills();
                //
                if(nNum == 2)
                {
                    pAgent->lock();
                    pAgent->bLogin = FALSE;
                    pAgent->AgentState = AS_Notlogin;
                    SetAgentStatusReportData(pAgent);
                    SetAgentStatusReportData(pAgent);
                    pAgent->unlock();
                    m_pDlg->RemoveAgentFromMap(pAgent);

                }
            }
            else
            {
                //其余操作的处理！！！

                m_pDlg->m_Log.Log("FLOW_LOG->Station %s %s Succ, Msg: %s",msg.m_strStation.c_str(), msg.m_strOperation.c_str(), msg.m_strUserData.c_str());
                m_pDlg->m_IOOpt.SendMsgToUser(pAgent->sock,msg.m_strBody);
            }

            //最终返回给坐席的数据
            m_pDlg->m_Log.Log("TEST_LOG->response给坐席的数据：%s",msg.m_strBody.c_str());
            //最后Release掉座席对象的引用
            pAgent->Release();
        }
        else
        {
            //没有获取到座席对象
        }
    }
    else
    {
        //strBody为空
    }

    return TRUE;
}

BOOL CHandleAskPBX::HandleIVRLogout(const std::string &sEvt)
{
    char buffer[256];
    CGeneralUtils myGeneralUtil;
    myGeneralUtil.GetStringValue(sEvt,"MemberName",buffer);
    m_pDlg->m_Log.Log("IVR_LOG->IVR Logout MemberName = %s", buffer);
    std::string msg(buffer);
    size_t index1 = msg.find("/");
    if(index1 == std::string::npos)
        return FALSE;

    if(msg.substr(0, index1) != "SIP")
        return FALSE;

    size_t index2 = msg.find("@", index1+1);
    if(index2 == std::string::npos)
        return FALSE;

    if(index2 > index1+1)
    {
        m_pDlg->m_Log.Log("IVR_LOG->IVR Logout station = %s", msg.substr(index1+1, index2-index1-1).c_str());
        CAgent* pAgent =  m_pDlg->GetAgentFromStation(msg.substr(index1+1, index2-index1-1));
        if(pAgent)
        {
            pAgent->lock();
            if(pAgent->m_strSource == "ivr")
            {
                pAgent->bLogin = FALSE;
                pAgent->AgentState = AS_Notlogin;
                SetAgentStatusReportData(pAgent);
                SetAgentStatusReportData(pAgent);
                pAgent->unlock();
                m_pDlg->RemoveAgentFromMap(pAgent);
                pAgent->Release();
                m_pDlg->m_Log.Log("IVR_LOG->IVR Logout station = %s success", msg.substr(index1+1, index2-index1-1).c_str());
            }
            else
            {
                m_pDlg->m_Log.Log("IVR_LOG->IVR Logout Agent source = %s not ivr", pAgent->m_strSource.c_str());
                pAgent->unlock();
                pAgent->Release();
            }
        }
    }

    return TRUE;
}




