/*
 *  This file is part of nzbget
 *
 *  Copyright (C) 2007-2015 Andrey Prygunkov <hugbug@users.sourceforge.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * $Revision$
 * $Date$
 *
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef WIN32
#include "win32.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifndef WIN32
#include <unistd.h>
#endif
#include <sys/stat.h>
#include <stdio.h>

#include "nzbget.h"
#include "QueueScript.h"
#include "NzbScript.h"
#include "Options.h"
#include "Log.h"
#include "Util.h"

static const char* QUEUE_EVENT_NAMES[] = { "FILE_DOWNLOADED", "URL_COMPLETED", "NZB_ADDED", "NZB_DOWNLOADED", "NZB_DELETED" };

class QueueScriptController : public Thread, public NZBScriptController
{
private:
	char*				m_szNZBName;
	char*				m_szNZBFilename;
	char*				m_szUrl;
	char*				m_szCategory;
	char*				m_szDestDir;
	int					m_iID;
	int					m_iPriority;
	char*				m_szDupeKey;
	EDupeMode			m_eDupeMode;
	int					m_iDupeScore;
	NZBParameterList	m_Parameters;
	int					m_iPrefixLen;
	ScriptConfig::Script*	m_pScript;
	QueueScriptCoordinator::EEvent	m_eEvent;
	bool				m_bMarkBad;
	NZBInfo::EDeleteStatus m_eDeleteStatus;
	NZBInfo::EUrlStatus	m_eUrlStatus;

	void				PrepareParams(const char* szScriptName);

protected:
	virtual void		ExecuteScript(ScriptConfig::Script* pScript);
	virtual void		AddMessage(Message::EKind eKind, const char* szText);

public:
	virtual				~QueueScriptController();
	virtual void		Run();
	static void			StartScript(NZBInfo* pNZBInfo, ScriptConfig::Script* pScript, QueueScriptCoordinator::EEvent eEvent);
};


QueueScriptController::~QueueScriptController()
{
	free(m_szNZBName);
	free(m_szNZBFilename);
	free(m_szUrl);
	free(m_szCategory);
	free(m_szDestDir);
	free(m_szDupeKey);
}

void QueueScriptController::StartScript(NZBInfo* pNZBInfo, ScriptConfig::Script* pScript, QueueScriptCoordinator::EEvent eEvent)
{
	QueueScriptController* pScriptController = new QueueScriptController();

	pScriptController->m_szNZBName = strdup(pNZBInfo->GetName());
	pScriptController->m_szNZBFilename = strdup(pNZBInfo->GetFilename());
	pScriptController->m_szUrl = strdup(pNZBInfo->GetURL());
	pScriptController->m_szCategory = strdup(pNZBInfo->GetCategory());
	pScriptController->m_szDestDir = strdup(pNZBInfo->GetDestDir());
	pScriptController->m_iID = pNZBInfo->GetID();
	pScriptController->m_iPriority = pNZBInfo->GetPriority();
	pScriptController->m_szDupeKey = strdup(pNZBInfo->GetDupeKey());
	pScriptController->m_eDupeMode = pNZBInfo->GetDupeMode();
	pScriptController->m_iDupeScore = pNZBInfo->GetDupeScore();
	pScriptController->m_Parameters.CopyFrom(pNZBInfo->GetParameters());
	pScriptController->m_pScript = pScript;
	pScriptController->m_eEvent = eEvent;
	pScriptController->m_iPrefixLen = 0;
	pScriptController->m_bMarkBad = false;
	pScriptController->m_eDeleteStatus = pNZBInfo->GetDeleteStatus();
	pScriptController->m_eUrlStatus = pNZBInfo->GetUrlStatus();
	pScriptController->SetAutoDestroy(true);

	pScriptController->Start();
}

void QueueScriptController::Run()
{
	ExecuteScript(m_pScript);

	SetLogPrefix(NULL);

	if (m_bMarkBad)
	{
		DownloadQueue* pDownloadQueue = DownloadQueue::Lock();
		NZBInfo* pNZBInfo = pDownloadQueue->GetQueue()->Find(m_iID);
		if (pNZBInfo)
		{
			PrintMessage(Message::mkWarning, "Cancelling download and deleting %s", m_szNZBName);
			pNZBInfo->SetDeleteStatus(NZBInfo::dsBad);
			pDownloadQueue->EditEntry(m_iID, DownloadQueue::eaGroupDelete, 0, NULL);
		}
		DownloadQueue::Unlock();
	}

	g_pQueueScriptCoordinator->CheckQueue();
}

void QueueScriptController::ExecuteScript(ScriptConfig::Script* pScript)
{
	PrintMessage(m_eEvent == QueueScriptCoordinator::qeFileDownloaded ? Message::mkDetail : Message::mkInfo,
		"Executing queue-script %s for %s", pScript->GetName(), Util::BaseFileName(m_szNZBName));

	SetScript(pScript->GetLocation());
	SetArgs(NULL, false);

	char szInfoName[1024];
	snprintf(szInfoName, 1024, "queue-script %s for %s", pScript->GetName(), Util::BaseFileName(m_szNZBName));
	szInfoName[1024-1] = '\0';
	SetInfoName(szInfoName);

	SetLogPrefix(pScript->GetDisplayName());
	m_iPrefixLen = strlen(pScript->GetDisplayName()) + 2; // 2 = strlen(": ");
	PrepareParams(pScript->GetName());

	Execute();

	SetLogPrefix(NULL);
}

void QueueScriptController::PrepareParams(const char* szScriptName)
{
	ResetEnv();

	SetEnvVar("NZBNA_NZBNAME", m_szNZBName);
	SetIntEnvVar("NZBNA_NZBID", m_iID);
	SetEnvVar("NZBNA_FILENAME", m_szNZBFilename);
	SetEnvVar("NZBNA_DIRECTORY", m_szDestDir);
	SetEnvVar("NZBNA_URL", m_szUrl);
	SetEnvVar("NZBNA_CATEGORY", m_szCategory);
	SetIntEnvVar("NZBNA_PRIORITY", m_iPriority);
	SetIntEnvVar("NZBNA_LASTID", m_iID);	// deprecated

	SetEnvVar("NZBNA_DUPEKEY", m_szDupeKey);
	SetIntEnvVar("NZBNA_DUPESCORE", m_iDupeScore);

	const char* szDupeModeName[] = { "SCORE", "ALL", "FORCE" };
	SetEnvVar("NZBNA_DUPEMODE", szDupeModeName[m_eDupeMode]);

	SetEnvVar("NZBNA_EVENT", QUEUE_EVENT_NAMES[m_eEvent]);

    const char* szDeleteStatusName[] = { "NONE", "MANUAL", "HEALTH", "DUPE", "BAD", "GOOD", "COPY", "SCAN" };
	SetEnvVar("NZBNA_DELETESTATUS", szDeleteStatusName[m_eDeleteStatus]);

	const char* szUrlStatusName[] = { "NONE", "UNKNOWN", "SUCCESS", "FAILURE", "UNKNOWN", "SCAN_SKIPPED", "SCAN_FAILURE" };
	SetEnvVar("NZBNA_URLSTATUS", szUrlStatusName[m_eUrlStatus]);

	PrepareEnvScript(&m_Parameters, szScriptName);
}

void QueueScriptController::AddMessage(Message::EKind eKind, const char* szText)
{
	const char* szMsgText = szText + m_iPrefixLen;

	if (!strncmp(szMsgText, "[NZB] ", 6))
	{
		debug("Command %s detected", szMsgText + 6);
		if (!strncmp(szMsgText + 6, "NZBPR_", 6))
		{
			char* szParam = strdup(szMsgText + 6 + 6);
			char* szValue = strchr(szParam, '=');
			if (szValue)
			{
				*szValue = '\0';
				DownloadQueue* pDownloadQueue = DownloadQueue::Lock();
				NZBInfo* pNZBInfo = pDownloadQueue->GetQueue()->Find(m_iID);
				if (pNZBInfo)
				{
					pNZBInfo->GetParameters()->SetParameter(szParam, szValue + 1);
				}
				DownloadQueue::Unlock();
			}
			else
			{
				error("Invalid command \"%s\" received from %s", szMsgText, GetInfoName());
			}
			free(szParam);
		}
		else if (!strncmp(szMsgText + 6, "MARK=BAD", 8))
		{
			m_bMarkBad = true;
			DownloadQueue* pDownloadQueue = DownloadQueue::Lock();
			NZBInfo* pNZBInfo = pDownloadQueue->GetQueue()->Find(m_iID);
			if (pNZBInfo)
			{
				SetLogPrefix(NULL);
				PrintMessage(Message::mkWarning, "Marking %s as bad", m_szNZBName);
				SetLogPrefix(m_pScript->GetDisplayName());
				pNZBInfo->SetMarkStatus(NZBInfo::ksBad);
			}
			DownloadQueue::Unlock();
		}
		else
		{
			error("Invalid command \"%s\" received from %s", szMsgText, GetInfoName());
		}
	}
	else
	{
		ScriptController::AddMessage(eKind, szText);
	}
}


QueueScriptCoordinator::QueueItem::QueueItem(int iNZBID, ScriptConfig::Script* pScript, EEvent eEvent)
{
	m_iNZBID = iNZBID;
	m_pScript = pScript;
	m_eEvent = eEvent;
}

QueueScriptCoordinator::QueueScriptCoordinator()
{
	m_pCurItem = NULL;
	m_bStopped = false;
}

QueueScriptCoordinator::~QueueScriptCoordinator()
{
	delete m_pCurItem;
	for (Queue::iterator it = m_Queue.begin(); it != m_Queue.end(); it++ )
	{
		delete *it;
	}
}

void QueueScriptCoordinator::InitOptions()
{
	m_bHasQueueScripts = false;
	for (ScriptConfig::Scripts::iterator it = g_pScriptConfig->GetScripts()->begin(); it != g_pScriptConfig->GetScripts()->end(); it++)
	{
		ScriptConfig::Script* pScript = *it;
		if (pScript->GetQueueScript())
		{
			m_bHasQueueScripts = true;
			break;
		}
	}
}

void QueueScriptCoordinator::EnqueueScript(NZBInfo* pNZBInfo, EEvent eEvent)
{
	if (!m_bHasQueueScripts)
	{
		return;
	}

	m_mutexQueue.Lock();

	if (eEvent == qeNzbDownloaded)
	{
		// delete all other queued scripts for this nzb
		for (Queue::iterator it = m_Queue.begin(); it != m_Queue.end(); )
		{
			QueueItem* pQueueItem = *it;
			if (pQueueItem->GetNZBID() == pNZBInfo->GetID())
			{
				delete pQueueItem;
				it = m_Queue.erase(it);
				continue;
			}
			it++;
		}
	}

	// respect option "EventInterval"
	time_t tCurTime = time(NULL);
	if (eEvent == qeFileDownloaded &&
		(g_pOptions->GetEventInterval() == -1 ||
		 (g_pOptions->GetEventInterval() > 0 && tCurTime - pNZBInfo->GetQueueScriptTime() > 0 &&
		 (int)(tCurTime - pNZBInfo->GetQueueScriptTime()) < g_pOptions->GetEventInterval())))
	{
		m_mutexQueue.Unlock();
		return;
	}

	for (ScriptConfig::Scripts::iterator it = g_pScriptConfig->GetScripts()->begin(); it != g_pScriptConfig->GetScripts()->end(); it++)
	{
		ScriptConfig::Script* pScript = *it;

		if (!pScript->GetQueueScript())
		{
			continue;
		}

		bool bUseScript = false;

		// check queue-scripts
		const char* szQueueScript = g_pOptions->GetQueueScript();
		if (!Util::EmptyStr(szQueueScript))
		{
			// split szQueueScript into tokens
			Tokenizer tok(szQueueScript, ",;");
			while (const char* szScriptName = tok.Next())
			{
				if (Util::SameFilename(szScriptName, pScript->GetName()))
				{
					bUseScript = true;
					break;
				}
			}
		}

		// check post-processing-scripts
		if (!bUseScript)
		{
			for (NZBParameterList::iterator it = pNZBInfo->GetParameters()->begin(); it != pNZBInfo->GetParameters()->end(); it++)
			{
				NZBParameter* pParameter = *it;
				const char* szVarname = pParameter->GetName();
				if (strlen(szVarname) > 0 && szVarname[0] != '*' && szVarname[strlen(szVarname)-1] == ':' &&
					(!strcasecmp(pParameter->GetValue(), "yes") ||
					 !strcasecmp(pParameter->GetValue(), "on") ||
					 !strcasecmp(pParameter->GetValue(), "1")))
				{
					char szScriptName[1024];
					strncpy(szScriptName, szVarname, 1024);
					szScriptName[1024-1] = '\0';
					szScriptName[strlen(szScriptName)-1] = '\0'; // remove trailing ':'
					if (Util::SameFilename(szScriptName, pScript->GetName()))
					{
						bUseScript = true;
						break;
					}
				}
			}
		}

		bUseScript &= Util::EmptyStr(pScript->GetQueueEvents()) || strstr(pScript->GetQueueEvents(), QUEUE_EVENT_NAMES[eEvent]);

		if (bUseScript)
		{
			bool bAlreadyQueued = false;
			if (eEvent == qeFileDownloaded)
			{
				// check if this script is already queued for this nzb
				for (Queue::iterator it2 = m_Queue.begin(); it2 != m_Queue.end(); it2++)
				{
					QueueItem* pQueueItem = *it2;
					if (pQueueItem->GetNZBID() == pNZBInfo->GetID() && pQueueItem->GetScript() == pScript)
					{
						bAlreadyQueued = true;
						break;
					}
				}
			}

			if (!bAlreadyQueued)
			{
				QueueItem* pQueueItem = new QueueItem(pNZBInfo->GetID(), pScript, eEvent);
				if (m_pCurItem)
				{
					m_Queue.push_back(pQueueItem);
				}
				else
				{
					StartScript(pNZBInfo, pQueueItem);
				}
			}

			pNZBInfo->SetQueueScriptTime(time(NULL));
		}
	}

	m_mutexQueue.Unlock();
}

NZBInfo* QueueScriptCoordinator::FindNZBInfo(DownloadQueue* pDownloadQueue, int iNZBID)
{
	NZBInfo* pNZBInfo = pDownloadQueue->GetQueue()->Find(iNZBID);
	if (!pNZBInfo)
	{
		for (HistoryList::iterator it = pDownloadQueue->GetHistory()->begin(); it != pDownloadQueue->GetHistory()->end(); it++)
		{
			HistoryInfo* pHistoryInfo = *it;
			if (pHistoryInfo->GetNZBInfo() && pHistoryInfo->GetNZBInfo()->GetID() == iNZBID)
			{
				pNZBInfo = pHistoryInfo->GetNZBInfo();
				break;
			}
		}
	}
	return pNZBInfo;
}

void QueueScriptCoordinator::CheckQueue()
{
	if (m_bStopped)
	{
		return;
	}

	DownloadQueue* pDownloadQueue = DownloadQueue::Lock();
	m_mutexQueue.Lock();

	delete m_pCurItem;

	m_pCurItem = NULL;
	NZBInfo* pCurNZBInfo = NULL;
	Queue::iterator itCurItem = m_Queue.end();

	for (Queue::iterator it = m_Queue.begin(); it != m_Queue.end(); )
	{
		QueueItem* pQueueItem = *it;

		NZBInfo* pNZBInfo = FindNZBInfo(pDownloadQueue, pQueueItem->GetNZBID());

		// in a case this nzb must not be processed further - delete queue script from queue
		if (!pNZBInfo ||
			(pNZBInfo->GetDeleteStatus() != NZBInfo::dsNone && pQueueItem->GetEvent() != qeNzbDeleted) ||
			pNZBInfo->GetMarkStatus() == NZBInfo::ksBad)
		{
			delete pQueueItem;
			it = m_Queue.erase(it);
			continue;
		}

		if (!m_pCurItem || pQueueItem->GetEvent() > m_pCurItem->GetEvent())
		{
			m_pCurItem = pQueueItem;
			itCurItem = it;
			pCurNZBInfo = pNZBInfo;
		}

		it++;
	}

	if (m_pCurItem)
	{
		m_Queue.erase(itCurItem);
		StartScript(pCurNZBInfo, m_pCurItem);
	}

	m_mutexQueue.Unlock();
	DownloadQueue::Unlock();
}

void QueueScriptCoordinator::StartScript(NZBInfo* pNZBInfo, QueueItem* pQueueItem)
{
	m_pCurItem = pQueueItem;
	QueueScriptController::StartScript(pNZBInfo, pQueueItem->GetScript(), pQueueItem->GetEvent());
}

bool QueueScriptCoordinator::HasJob(int iNZBID, bool* pActive)
{
	m_mutexQueue.Lock();
	bool bWorking = m_pCurItem && m_pCurItem->GetNZBID() == iNZBID;
	if (pActive)
	{
		*pActive = bWorking;
	}
	if (!bWorking)
	{
		for (Queue::iterator it = m_Queue.begin(); it != m_Queue.end(); it++)
		{
			QueueItem* pQueueItem = *it;
			bWorking = pQueueItem->GetNZBID() == iNZBID;
			if (bWorking)
			{
				break;
			}
		}
	}
	m_mutexQueue.Unlock();

	return bWorking;
}

int QueueScriptCoordinator::GetQueueSize()
{
	m_mutexQueue.Lock();
	int iQueuedCount = m_Queue.size();
	if (m_pCurItem)
	{
		iQueuedCount++;
	}
	m_mutexQueue.Unlock();

	return iQueuedCount;
}
