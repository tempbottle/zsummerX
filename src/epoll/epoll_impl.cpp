﻿/*
 * zsummerX License
 * -----------
 * 
 * zsummerX is licensed under the terms of the MIT license reproduced below.
 * This means that zsummerX is free software and can be used for both academic
 * and commercial purposes at absolutely no cost.
 * 
 * 
 * ===============================================================================
 * 
 * Copyright (C) 2010-2015 YaweiZhang <yawei.zhang@foxmail.com>.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 * 
 * ===============================================================================
 * 
 * (end of COPYRIGHT)
 */


#include <zsummerX/epoll/epoll_impl.h>
#include <zsummerX/epoll/tcpsocket_impl.h>
#include <zsummerX/epoll/tcpaccept_impl.h>
#include <zsummerX/epoll/udpsocket_impl.h>

using namespace zsummer::network;

bool EventLoop::initialize()
{
    if (_epoll != InvalideFD)
    {
        LCF("EventLoop::initialize[this0x"<<this <<"] epoll is created ! " << logSection());
        return false;
    }
    const int IGNORE_ENVENTS = 100;
    _epoll = epoll_create(IGNORE_ENVENTS);
    if (_epoll == InvalideFD)
    {
        LCF("EventLoop::initialize[this0x" << this << "] create epoll err errno=" << strerror(errno) << logSection());
        return false;
    }

    if (socketpair(AF_LOCAL, SOCK_STREAM, 0, _sockpair) != 0)
    {
        LCF("EventLoop::initialize[this0x" << this << "] create socketpair.  errno=" << strerror(errno) << logSection());
        return false;
    }
    setNonBlock(_sockpair[0]);
    setNonBlock(_sockpair[1]);
    setNoDelay(_sockpair[0]);
    setNoDelay(_sockpair[1]);

    _register._event.data.ptr = &_register;
    _register._event.events = EPOLLIN;
    _register._fd = _sockpair[1];
    _register._linkstat = LS_ESTABLISHED;
    _register._type = tagRegister::REG_ZSUMMER;
    if (!registerEvent(EPOLL_CTL_ADD, _register))
    {
        LCF("EventLoop::initialize[this0x" << this << "] EPOLL_CTL_ADD _socketpair error. " << logSection());
        return false;
    }
    
    return true;
}

bool EventLoop::registerEvent(int op, tagRegister & reg)
{
    if (epoll_ctl(_epoll, op, reg._fd, &reg._event) != 0)
    {
        return false;
    }
    return true;
}

void EventLoop::PostMessage(_OnPostHandler &&handle)
{
    _OnPostHandler * pHandler = new _OnPostHandler(std::move(handle));
    _stackMessagesLock.lock();
    if (_stackMessages.empty()){char c = '0'; send(_sockpair[0], &c, 1, 0);}
    _stackMessages.push_back(pHandler);
    _stackMessagesLock.unlock();

}

std::string EventLoop::logSection()
{
    std::stringstream os;
    _stackMessagesLock.lock();
    MessageStack::size_type msgSize = _stackMessages.size();
    _stackMessagesLock.unlock();
    os << " EventLoop: _epoll=" << _epoll << ", _sockpair[2]={" << _sockpair[0] << "," << _sockpair[1] << "}"
        << " _stackMessages.size()=" << msgSize << ", current total timer=" << _timer.getTimersCount()
        << " _register=" << _register;
    return os.str();
}

void EventLoop::runOnce(bool isImmediately)
{
    int retCount = epoll_wait(_epoll, _events, 1000,  isImmediately ? 0 : _timer.getNextExpireTime());
    if (retCount == -1)
    {
        if (errno != EINTR)
        {
            LCW("EventLoop::runOnce[this0x" << this << "]  epoll_wait err!  errno=" << strerror(errno) << logSection());
            return; //! error
        }
        return;
    }

    //check timer
    {
        _timer.checkTimer();
        if (retCount == 0) return;//timeout
    }


    for (int i=0; i<retCount; i++)
    {
        int eventflag = _events[i].events;
        tagRegister * pReg = (tagRegister *)_events[i].data.ptr;
        //tagHandle  type
        if (pReg->_type == tagRegister::REG_ZSUMMER)
        {
            char buf[1000];
            while (recv(pReg->_fd, buf, 1000, 0) > 0);

            MessageStack msgs;
            _stackMessagesLock.lock();
            msgs.swap(_stackMessages);
            _stackMessagesLock.unlock();

            for (auto pfunc : msgs)
            {
                _OnPostHandler * p = (_OnPostHandler*)pfunc;
                try
                {
                    (*p)();
                }
                catch (std::runtime_error e)
                {
                    LCW("OnPostHandler have runtime_error exception. err=" << e.what());
                }
                catch (...)
                {
                    LCW("OnPostHandler have unknown exception.");
                }
                delete p;
            }
        }
        else if (pReg->_type == tagRegister::REG_TCP_ACCEPT)
        {
            if (eventflag & EPOLLIN)
            {
                if (pReg->_tcpacceptPtr)
                {
                    pReg->_tcpacceptPtr->onEPOLLMessage(true);
                }
            }
            else if (eventflag & EPOLLERR || eventflag & EPOLLHUP)
            {
                if (pReg->_tcpacceptPtr)
                {
                    pReg->_tcpacceptPtr->onEPOLLMessage(false);
                }
            }
        }
        else if (pReg->_type == tagRegister::REG_TCP_SOCKET)
        {
            if (eventflag & EPOLLERR || eventflag & EPOLLHUP)
            {
                if (pReg->_tcpSocketConnectPtr)
                {
                    pReg->_tcpSocketConnectPtr->onEPOLLMessage(EPOLLOUT, true);
                }
                else if (pReg->_tcpSocketRecvPtr)
                {
                    pReg->_tcpSocketRecvPtr->onEPOLLMessage(EPOLLIN, true);
                }
                else if (pReg->_tcpSocketSendPtr)
                {
                    pReg->_tcpSocketSendPtr->onEPOLLMessage(EPOLLOUT, true);
                }
            }
            else if (eventflag & EPOLLIN)
            {
                if (pReg->_tcpSocketRecvPtr)
                {
                    pReg->_tcpSocketRecvPtr->onEPOLLMessage(EPOLLIN, false);
                }
            }
            else if (eventflag & EPOLLOUT)
            {
                if (pReg->_tcpSocketConnectPtr)
                {
                    pReg->_tcpSocketConnectPtr->onEPOLLMessage(EPOLLOUT, false);
                }
                else if (pReg->_tcpSocketSendPtr)
                {
                    pReg->_tcpSocketSendPtr->onEPOLLMessage(EPOLLOUT, false);
                }
            }
        }
        else if (pReg->_type == tagRegister::REG_UDP_SOCKET)
        {
            if (pReg->_udpsocketPtr)
            {
                pReg->_udpsocketPtr->onEPOLLMessage(pReg->_type, eventflag);
            }
        }
        else
        {
            LCE("EventLoop::runOnce[this0x" << this << "] check register event type failed !!  type=" << pReg->_type << logSection());
        }
            
    }
}


