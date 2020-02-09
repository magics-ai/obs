#include "Proactor.h"
#include <ace/Message_Queue.h>
#include <ace/Asynch_IO.h>
#include <ace/OS_main.h>
#include <ace/Proactor.h>
#include <ace/Asynch_Acceptor.h>
#include <ace/Log_Msg.h>
#include <ace/OS_NS_time.h>
#include <iconv.h>
#include "../AskProxyDlg.h"

Server::Server()
{
    ACE_DEBUG((LM_DEBUG,"Server Handler 创建\n"));
    GetApp()->m_AgentLog.Log("FLOW_LOG->new connection");
}

Server::~Server()
{
//    ACE_DEBUG((LM_DEBUG,"Server 析构"));

    closeServer();
}

//open
void Server::open(ACE_HANDLE handler,ACE_Message_Block& message_block)
{
//    ACE_DEBUG((LM_DEBUG,"open function"));
    handle(handler);

    if(ws_.open(*this) == -1)
    {
        ACE_ERROR((LM_ERROR,
                   ACE_TEXT("(%t) %p\n"),
                   ACE_TEXT("Client::ACE_Asynch_Write_Stream::open")));
    }
    else if(rs_.open(*this) == -1)
    {
        ACE_ERROR((LM_ERROR,
                   ACE_TEXT("(%t) %p\n"),
                   ACE_TEXT("Client::ACE_Asynch_Read_Stream::open")));
    }
    else if(initiate_read_stream () != -1)
    {
        return;
    }
    //delete this;
}

void Server::closeServer()
{
    if(handle() != ACE_INVALID_HANDLE)
    {
        ACE_OS::closesocket(handle());
        handle(ACE_INVALID_HANDLE);
    }
}
//handle_read_stream
void Server::handle_read_stream(const ACE_Asynch_Read_Stream::Result& result)
{

//    ACE_DEBUG((LM_DEBUG," handle read stream\n"));

    ACE_Message_Block& mblk = result.message_block();

    if(!result.success() || result.bytes_transferred() == 0)
    {
        //delete this;
        return;
    }
    /*
    else if(result.bytes_transferred() < result.bytes_to_read())
    {
    	ACE_DEBUG((LM_DEBUG,"aaaaaaaaaaaaaaa\n"));
    	rs_.read(mblk,result.bytes_to_read() - result.bytes_transferred());
    }
    */
    else
    {
        ACE_Message_Block* mb = 0;
        ACE_NEW_NORETURN(mb,ACE_Message_Block(ACE_DEFAULT_CDR_BUFSIZE));
        mb = mblk.clone();
        process_message(mb);
        mb->release();
        mblk.release();

        ACE_NEW_NORETURN(mb,ACE_Message_Block(ACE_DEFAULT_CDR_BUFSIZE));
        rs_.read(*mb,mb->space());

    }
}
//handle_write_stream
void Server::handle_write_stream(const ACE_Asynch_Write_Stream::Result& result)
{
    ACE_Message_Block& mblk = result.message_block();

    if(!result.success() || result.bytes_transferred() == 0)
    {
        mblk.release();
    }
    else if(result.bytes_transferred() < result.bytes_to_write())
    {
        ws_.write(mblk,result.bytes_to_write()-result.bytes_transferred());
    }
    else
    {
        mblk.release();
    }
}
//initiate_read_stream
int Server::initiate_read_stream(void)
{
    if(handle() == ACE_INVALID_HANDLE)
        return -1;
    ACE_Message_Block *mb;
    ACE_NEW_RETURN(mb,ACE_Message_Block(ACE_DEFAULT_CDR_BUFSIZE),-1);
    if(rs_.read(*mb,mb->space()) != 0)
    {
        mb->release();
        mb = 0;
        ACE_ERROR_RETURN((LM_ERROR,ACE_TEXT("(%t) Server %p\n"),ACE_TEXT("read")),-1);
    }
    return 0;
}

int Server::initiate_write_stream(ACE_Message_Block& mb,size_t nbytes)
{
    if(handle() == ACE_INVALID_HANDLE)
        return -1;
    return 0;
}
//declare send ....
//void Server::send_to_softphone(CStdString msg)
void Server::send_to_softphone(const std::string &msg)
{
    char* utf = const_cast<char*>(msg.c_str());
    size_t inlen = msg.length();
    size_t len = 4 * msg.length();
    char *gbk = new char[len];
    memset(gbk, 0, len);

    char *in = utf;
    char *out = gbk;

    iconv_t cd = iconv_open("GBK","UTF-8");
    iconv(cd, &in, &inlen, &out, &len);
    iconv_close(cd);
    //::send(handle(), msg.c_str(), msg.length(), 0);
    //theApp.m_AgentLog.Log("FLOW_LOG->%s",msg.c_str());
    if(send(handle(), gbk, strlen(gbk), 0) <= 0)
    {
        GetApp()->m_CheckAgent.m_CheckAgentLog.Log("ERROR_LOG->cti和座席socket连接异常: %s", strerror(errno));
    }
    //theApp.m_AgentLog.Log("FLOW_LOG->%s",msg.c_str());
    if(msg == "keep alive")
        GetApp()->m_keepLive.Log("FLOW_LOG->keep alive");
    else
        GetApp()->m_AgentLog.Log("FLOW_LOG->send to softphone: %s",msg.c_str());

    delete [] gbk;

    //ACE_Message_Block *mb = new  ACE_Message_Block(msg.length()+1);
    //mb->copy(msg);
    //mb->copy(msg.c_str());
    //start_write(mb);
}

void Server::start_write(ACE_Message_Block* mb)
{
    ws_.write(*mb,mb->length());
}
//消息处理
bool Server::process_message(ACE_Message_Block* mblk)
{
    char* pbuf = (char*)mblk->rd_ptr();
//	ACE_DEBUG((LM_DEBUG,"%s",pbuf));

    GetApp()->m_AgentLog.Log("FLOW_LOG->new process message");

    AgentMsg* msg = new AgentMsg;
    msg->s = this;
    msg->sMsg = pbuf;
    //CAskProxyDlg *pDlg = &theApp;
    CAskProxyDlg *pDlg = GetApp();

//	if(msg->sMsg.Left(10) == "keep alive")
//    std::cout << "|||server Process_message: " << pbuf << "|||" << std::endl;
    if(msg->sMsg.find("keep alive") == 0)
    {
//        ACE_DEBUG((LM_DEBUG,"ACE_DEBUG keep alive 心跳\n"));
        size_t nPos1=msg->sMsg.find("|");
        size_t nPos2=msg->sMsg.find("|",nPos1+1);

        std::string sStation;
        if(nPos1!=std::string::npos && nPos2!=std::string::npos)
        {
//            std::cout << "pos1, pos2" << std::endl;
            //处理keep alive数据
            //sStation = msg->sMsg.Mid(nPos1+1,nPos2-nPos1-1);
            sStation = msg->sMsg.substr(nPos1+1,nPos2-nPos1-1);
            //pDlg->SetLock();
            //std::map<std::string, CAgent*>::iterator it = pDlg->m_MapStationToAgent.find(sStation);
            //auto it = pDlg->m_MapStationToAgent.find(sStation);
            CAgent *pAgent = pDlg->GetAgentFromStation(sStation);
            //if(it != pDlg->m_MapStationToAgent.end())
            if(pAgent)
            {
//                it->second->sock = this;
//                it->second->preLiveTime = ACE_Date_Time();
//                it->second->preLiveTimeSecond = time(NULL);
                pAgent->lock();
                pAgent->sock = this;
                pAgent->preLiveTime = ACE_Date_Time();
                pAgent->preLiveTimeSecond = time(NULL);
                pAgent->unlock();
//                std::cout << "find Agent" << std::endl;
                /*---------------------------------------------------------------------
                        struct tm date = {0};
                        time_t begin_date;
                        ACE_OS::localtime_r(&begin_date,&date);
                        int month = date.tm_mon+1;
                        int year = date.tm_year+1900;
                        CStdString priTime;
                        priTime.Format("%.4d-%.2d%-%.2d %.2d:%.2d:%.2d",
                			year,month,date.tm_mday,date.tm_hour,date.tm_min,date.tm_sec);
                */
                //----------------------------------------------------------------------------
                send_to_softphone("keep alive");
                pAgent->Release();
            }
            else
            {
                pDlg->m_keepLive.Log("ERROR_LOG->无效的KeepAlive,	没有发现指定的坐席. %s",sStation.c_str());
            }
            //pDlg->SetUnLock();

            pDlg->m_keepLive.Log("FLOW_LOG->%s",msg->sMsg.c_str());
        }
        delete msg;
    }
    else
    {
        //将消息放入链表中。供其它线程处理!!
        pDlg->m_SoftPhoneMsgCritical.acquire();
        pDlg->m_AgentMsgList.push_back(msg);
        pDlg->m_SoftPhoneMsgCritical.release();
        pDlg->m_HandleSoftPhoneThread.m_hStartHandleSoftPhoneMsg.release();
    }
    return false;
}
