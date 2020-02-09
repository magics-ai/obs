#include "ResponseMsg.h"
#include "GenCMD.h"
#include "GeneralUtils.h"
#include "logclass/log.h"

CResponseMsg::CResponseMsg()
{
	m_strRet = "";
	m_strUserData = "";
	m_strStation = "";
	m_strOperation = "";
	m_tenantID = "NULL";
}

CResponseMsg::~CResponseMsg()
{

}
//置闲
/*
Response: Success
ActionID: SetIdle|2001|2182a9cd-9aeb-4ce5-98ed-dd416b0f2769
Message: Interface paused successfully
*/

//返回信息
/*
strBody = "<AskPT>\
<Source  Value=AskProxy />\
<Response  Value=SetIdle />\
<Body>\
<Result>Succ</Result>\
<UserData>[UserData]</UserData>\
</Body>\
</AskPT>";
*/
/*
Response=Success;
ActionID=ucid|50041|getVal|f5b76f25-c4bf-459e-a2cc-aa1bcb44611b;
Variable=x-sinosoftcrm-ucid;
Value=10-11-196-36-2010-11-03-15-46-25-1288770383-256
*/
std::string CResponseMsg::ParaAskPBXResponse(const std::string &sDoc)
{
	CGeneralUtils myGeneralUtils;

	char szActionID[128];
	memset(szActionID,0,128);
	myGeneralUtils.GetStringValue(sDoc,"ActionID",szActionID);
	if(strcmp(szActionID,"") == 0)
	{
		return "";
	}
	else
	{
		m_strActionID = szActionID;
	}


	//获取结果
	char szResponse[64];
	memset(szResponse,0,64);
	myGeneralUtils.GetStringValue(sDoc,_T("Response"),szResponse);
	if(strcmp(szResponse,"") == 0)
	{
		return "";
	}


	char szMessage[128];
	memset(szMessage,0,128);
	myGeneralUtils.GetStringValue(sDoc,"Message",szMessage);
	if(strcmp(szMessage,"") == 0)
	{
		return "";
	}


	std::string strStation;
	std::string strOpertion;
	std::string sAction(szActionID);

	//发现Station
	size_t n1 = sAction.find("|",0);
	size_t n2 = sAction.find("|",n1+1);
	size_t n3 = sAction.find("|",n2+1);
	size_t n4 = sAction.find("|",n3+1);
	if(n1==std::string::npos || n2==std::string::npos)
	{
		return "";
	}
	else
	{
		m_strOperation = sAction.substr(0, n1);
	    m_strStation = sAction.substr(n1+1,n2-n1-1);
		m_strRet = szResponse;

		//////////////////////////////////////////////////////////////////////////
		//2013-01-21 add by chenlin
		//@将pbx返回的错误转义一下，发给软电话

/*		添加技能组错误解释：
			"	'Queue' not specified.：AMI请求没有指定技能组
			"	'Interface' not specified.：AMI请求没有指定成员接口
			"	Unable to add interface: Already there：技能组中已经存在该成员
			"	Unable to add interface to queue: No such queue：PBX没有该技能组
			"	Out of memory：PBX内部错误
			删除技能组错误解释：
			"	Need 'Queue' and 'Interface' parameters.：AMI请求没有指定技能组或者成员接口
			"	Unable to remove interface: Not there：技能组没有该成员
			"	Unable to remove interface from queue: No such queue：PBX没有该技能组
			"	Out of memory：PBX内部错误
			"	Member not dynamic：该成员不是动态成员
			暂停/取消暂停技能组错误解释：
			"	Need 'Interface' and 'Paused' parameters.：AMI请求没有指定成员或者"Paused"参数
			"	Interface not found：没有该成员
*/
		//	Unable to add interface: Already there和
		//	Unable to remove interface: Not there这两个不向坐席发送错误，暂不处理
		if (strcmp(szMessage, "'Queue' not specified")==0)
		{
			m_strUserData = _T("AMI请求没有指定技能组");
		}
		else if (strcmp(szMessage, "'Interface' not specified")==0)
		{
			m_strUserData = _T("AMI请求没有指定成员接口");
		}
		else if (strcmp(szMessage, "Unable to add interface to queue=No such queue")==0)
		{
			m_strUserData = _T("PBX没有该技能组");
		}
		else if (strcmp(szMessage, "Out of memory")==0)
		{
			m_strUserData = _T("PBX内部错误");
		}
		else if (strcmp(szMessage, "Need 'Queue' and 'Interface' parameters.")==0)
		{
			m_strUserData = _T("AMI请求没有指定技能组或者成员接口");
		}
		else if (strcmp(szMessage, "Unable to remove interface from queue=No such queue")==0)
		{
			m_strUserData = _T("PBX没有该技能组");
		}
		else if (strcmp(szMessage, "Member not dynamic")==0)
		{
			m_strUserData = _T("该成员不是动态成员");
		}
		else
		{
			m_strUserData = szMessage;
		}
	//////////////////////////////////////////////////////////////////////////
	//	m_strUserData = szMessage;
	}
	if (n3 != std::string::npos && n4!=std::string::npos)
	{
		m_strQueue = sAction.substr(n3+1,n4-n3-1);
	}

	//发现action
	std::string strBody = "<AskPT>\
<Source  Value=AskProxy />\
<Response  Value=[Response] />\
<Body>\
<Result>[Result]</Result>\
<UserData>[UserData]</UserData>\
<AgentId>[AgentId]</AgentId>\
<TenantId>[TenantId]</TenantId>\
<Station>[Station]</Station>\
<Time>[Time]</Time>\
<PBXIP>[PBXIP]</PBXIP>\
<Skill>[Skill]</Skill>\
</Body>\
</AskPT>";

	if(m_strRet == "Success")
	{
		m_strRet = "Succ";
	}
	else
		m_strRet = "Fail";

	REPLACE(strBody, "[Response]", m_strOperation);
	REPLACE(strBody, "[Result]", m_strRet);
	REPLACE(strBody, "[UserData]", m_strUserData);
	REPLACE(strBody, "[AgentId]",m_strAgentId);
	REPLACE(strBody, "[Station]",m_strStation);
	REPLACE(strBody, "[Time]",m_strTime);
	REPLACE(strBody, "[TenantId]", m_tenantID);
	REPLACE(strBody, "[PBXIP]", m_pbxIP);
	REPLACE(strBody, "[Skill]", m_strQueue);

	strBody += "\r\n\r\n";

	m_strBody = strBody;

	return strBody;
}
//20121101  增加
std::string  CResponseMsg::EncodeResp()
{
	//发现action
	std::string strBody = "<AskPT>\
<Source  Value=AskProxy />\
<Response  Value=[Response] />\
<Body>\
<Result>[Result]</Result>\
<UserData>[UserData]</UserData>\
<AgentId>[AgentId]</AgentId>\
<TenantId>[TenantId]</TenantId>\
<Station>[Station]</Station>\
<Time>[Time]</Time>\
<PBXIP>[PBXIP]</PBXIP>\
<Skill>[Skill]</Skill>\
</Body>\
</AskPT>";

	REPLACE(strBody, "[Response]", m_strOperation);
	REPLACE(strBody, "[Result]", m_strRet);
	REPLACE(strBody, "[UserData]", m_strUserData);
	REPLACE(strBody, "[AgentId]",m_strAgentId);
	REPLACE(strBody, "[Station]",m_strStation);
	REPLACE(strBody, "[Time]",m_strTime);
	REPLACE(strBody, "[TenantId]", m_tenantID);
	REPLACE(strBody, "[PBXIP]", m_pbxIP);
	REPLACE(strBody, "[Skill]", m_strQueue);

	strBody += "\r\n\r\n";

	return strBody;
}
