/*
 *  This file is part of nzbget
 *
 *  Copyright (C) 2015 Andrey Prygunkov <hugbug@users.sourceforge.net>
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


#ifndef SERVICE_H
#define SERVICE_H

#include <vector>

#include "Thread.h"

class Service
{
private:
	int					m_iLastTick;

protected:
	virtual int			ServiceInterval() = 0;
	virtual void		ServiceWork() = 0;

	friend class ServiceCoordinator;

public:
						Service();
};

class ServiceCoordinator : public Thread
{
public:
	typedef std::vector<Service*> ServiceList;

private:
	ServiceList	m_Services;

	void				RegisterService(Service* pService);

	friend class Service;

public:
						ServiceCoordinator();
	virtual				~ServiceCoordinator();
	virtual void		Run();
};

extern ServiceCoordinator* g_pServiceCoordinator;

#endif
