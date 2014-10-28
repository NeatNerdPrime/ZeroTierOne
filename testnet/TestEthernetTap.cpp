/*
 * ZeroTier One - Global Peer to Peer Ethernet
 * Copyright (C) 2011-2014  ZeroTier Networks LLC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * --
 *
 * ZeroTier may be used and distributed under the terms of the GPLv3, which
 * are available at: http://www.gnu.org/licenses/gpl-3.0.html
 *
 * If you would like to embed ZeroTier into a commercial application or
 * redistribute it in a modified binary form, please contact ZeroTier Networks
 * LLC. Start here: http://www.zerotier.com/
 */

#include "TestEthernetTap.hpp"
#include "TestEthernetTapFactory.hpp"

#include "../node/Constants.hpp"
#include "../node/Utils.hpp"

#include <stdio.h>
#include <stdlib.h>

#ifdef __WINDOWS__
#include <process.h>
#else
#include <unistd.h>
#endif

namespace ZeroTier {

TestEthernetTap::TestEthernetTap(
	TestEthernetTapFactory *parent,
	const MAC &mac,
	unsigned int mtu,
	unsigned int metric,
	uint64_t nwid,
	const char *desiredDevice,
	const char *friendlyName,
	void (*handler)(void *,const MAC &,const MAC &,unsigned int,const Buffer<4096> &),
	void *arg) :
	EthernetTap("TestEthernetTap",mac,mtu,metric),
	_nwid(nwid),
	_parent(parent),
	_handler(handler),
	_arg(arg),
	_enabled(true)
{
	static volatile unsigned int testTapCounter = 0;

	char tmp[64];
	int pid = 0;
#ifdef __UNIX_LIKE__
	pid = (int)getpid();
#endif
#ifdef __WINDOWS__
	pid = (int)_getpid();
#endif
	Utils::snprintf(tmp,sizeof(tmp),"test%dtap%d",pid,testTapCounter++);
	_dev = tmp;

	_thread = Thread::start(this);
}

TestEthernetTap::~TestEthernetTap()
{
	static const TestFrame zf;
	{
		Mutex::Lock _l(_pq_m);
		_pq.push_back(zf); // 0 length frame = exit
		_pq_c.signal();
	}
	Thread::join(_thread);
}

void TestEthernetTap::setEnabled(bool en)
{
	_enabled = en;
}

bool TestEthernetTap::enabled() const
{
	return _enabled;
}

bool TestEthernetTap::addIP(const InetAddress &ip)
{
	return true;
}

bool TestEthernetTap::removeIP(const InetAddress &ip)
{
	return true;
}

std::set<InetAddress> TestEthernetTap::ips() const
{
	return std::set<InetAddress>();
}

void TestEthernetTap::put(const MAC &from,const MAC &to,unsigned int etherType,const void *data,unsigned int len)
{
	Mutex::Lock _l(_gq_m);
	_gq.push_back(TestFrame(from,to,data,etherType,len));
}

std::string TestEthernetTap::deviceName() const
{
	return _dev;
}

void TestEthernetTap::setFriendlyName(const char *friendlyName)
{
}

bool TestEthernetTap::updateMulticastGroups(std::set<MulticastGroup> &groups)
{
	return false;
}

bool TestEthernetTap::injectPacketFromHost(const MAC &from,const MAC &to,unsigned int etherType,const void *data,unsigned int len)
{
	if ((len == 0)||(len > 2800))
		return false;

	{
		Mutex::Lock _l(_pq_m);
		_pq.push_back(TestFrame(from,to,data,etherType & 0xffff,len));
		_pq_c.signal();
	}

	return true;
}

void TestEthernetTap::threadMain()
	throw()
{
	std::vector<TestFrame> q;
	for(;;) {
		{
			Mutex::Lock _l(_pq_m);
			q = _pq;
			_pq.clear();
		}

		for(std::vector<TestFrame>::iterator f(q.begin());f!=q.end();++f) {
			if (!f->len)
				return; // empty frame signals thread to die
			else if (_enabled) {
				try {
					_handler(_arg,f->from,f->to,f->etherType,Buffer<4096>(f->data,f->len));
				} catch ( ... ) {} // handlers should not throw
			}
		}

		_pq_c.wait(1000);
	}
}

} // namespace ZeroTier
