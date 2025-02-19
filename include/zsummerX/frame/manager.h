/*
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


#ifndef ZSUMMER_TCPSESSION_MANAGER_H_
#define ZSUMMER_TCPSESSION_MANAGER_H_

#include "config.h"
namespace zsummer
{
    namespace network
    {
        /*
            SessionManager是一个单例singleton, 是一个对zsummerX底层接口的高级封装, 如果需要自己封装 则可以参考frame的做法或者example中的例子进行封装或使用.
            这个单例提供了所有网络的高级的可操作接口, 比如启动网络模块单例, 开启网络循环, 依次关闭部分网络功能 最后退出网络循环,  添加多个监听接口, 添加多个连出, 发送数据,
                跨线程的通知机制post, 创建取消定时器, 获取连接信息, 设置定时检测, 设置断线重莲次数和间隔, 设置是否支持flash Policy, 设置最大可连入的连接数, 设置协议是二进制的TCP协议(proto4z协议流), 
                设置协议是HTTP(可做WEB服务器和客户端使用, 很方便的做一些SDK认证和平台接入),  可获取运行时的网络状态数据.
            消息处理的handler注册 参看文件dispatch.h
        */
        class SessionManager
        {
        private:
            SessionManager();

        public://!get the single and global object pointer   
            static SessionManager & getRef();
            inline static SessionManager * getPtr(){ return &getRef(); };
        public:
            //要使用SessionManager必须先调用start来启动服务.
            bool start();

            //该组接口说明:
            // stopXXXX系列接口可以在信号处理函数中调用, 也只有该系列函数可以在信号处理函数中使用.
            // 一些stopXXX接口提供完成通知, 但需要调用setStopXXXX去注册回调函数.
            void stopAccept();
            void stopClients();
            void setStopClientsHandler(std::function<void()> handler);
            void stopServers();
            void setStopServersHandler(std::function<void()> handler);

            //退出消息循环.
            void stop();

            //阻塞当前线程并开始消息循环. 默认选用这个比较好. 当希望有更细力度的控制run的时候推荐使用runOnce
            bool run();

            //执行一次消息处理, 如果isImmediately为true, 则无论当前处理有无数据 都需要立即返回, 可以嵌入到任意一个线程中灵活使用
            //默认为false,  如果没有网络消息和事件消息 则会阻塞一小段时间, 有消息通知会立刻被唤醒.
            bool runOnce(bool isImmediately = false);

            //handle: std::function<void()>
            //switch initiative, in the multi-thread it's switch call thread simultaneously.
            //投递一个handler到SessionManager的线程中去处理, 线程安全.
            template<class H>
            void post(H &&h){ _summer->post(std::move(h)); }

            //创建定时器 单位是毫秒 非线程安全, 如有多线程下的需求请配合POST来实现.
            template <class H>
            zsummer::network::TimerID createTimer(unsigned int delayms, H &&h){ return _summer->createTimer(delayms, std::move(h)); }
            //取消定时器.  注意, 如果在定时器的回调handler中取消当前定时器 会失败的.
            bool cancelTimer(unsigned long long timerID){ return _summer->cancelTimer(timerID); }


            //! add acceptor under the configure.
            AccepterID addAcceptor(const ListenConfig &traits);
            bool getAcceptorConfig(AccepterID aID, std::pair<ListenConfig, ListenInfo> & config);
            AccepterID getAccepterID(SessionID sID);

            //! add connector under the configure.
            SessionID addConnector(const ConnectConfig & traits);
            bool getConnectorConfig(SessionID sID, std::pair<ConnectConfig, ConnectInfo> & config);
            TcpSessionPtr getTcpSession(SessionID sID);

            //send data.
            void sendSessionData(SessionID sID, const char * orgData, unsigned int orgDataLen);
            //send data.
            void sendSessionData(SessionID sID, ProtoID pID, const char * userData, unsigned int userDataLen);

            //close session socket.
            void kickSession(SessionID sID);

        public:
            //statistical information
            //统计信息.
            std::string getRemoteIP(SessionID sID);
            unsigned short getRemotePort(SessionID sID);
            unsigned long long _totalConnectCount = 0;
            unsigned long long _totalAcceptCount = 0;
            unsigned long long _totalConnectClosedCount = 0;
            unsigned long long _totalAcceptClosedCount = 0;
            
            unsigned long long _totalSendCount = 0;
            unsigned long long _totalSendBytes = 0;
            unsigned long long _totalSendMessages = 0;
            unsigned long long _totalRecvCount = 0;
            unsigned long long _totalRecvBytes = 0;
            unsigned long long _totalRecvMessages = 0;
            unsigned long long _totalRecvHTTPCount = 0;
            time_t _openTime = 0;

        private:
            friend class TcpSession;
            // 一个established状态的session已经关闭. 该session是连入的.
            void onSessionClose(AccepterID aID, SessionID sID, const TcpSessionPtr &session);

            // 一个established状态的session已经关闭或者连接失败, 因为是connect 需要判断是否需要重连.
            void onConnect(SessionID cID, bool bConnected, const TcpSessionPtr &session);

            //accept到新连接.
            void onAcceptNewClient(zsummer::network::NetErrorCode ec, const TcpSocketPtr & s, const TcpAcceptPtr & accepter, AccepterID aID);
        private:

            //消息循环
            EventLoopPtr _summer;

            //! 以下一组参数均为控制消息循环的开启和关闭用的
            bool  _running = true;  //默认是开启, 否则会在合适的时候退出消息循环.
            bool _stopAccept = false; //停止accept新的连接.
            char _stopClients = 0; //关掉所有客户端连接.
            std::function<void()> _funClientsStop; // 所有客户端都被关闭后则执行这个回调.
            char _stopServers = 0; //关掉所有连出的连接.
            std::function<void()> _funServerStop; // 所有连出连接被关闭后执行的回调.


            //!以下一组ID用于生成对应的unique ID. 
            SessionID _lastAcceptID = 0; //accept ID sequence. range  [0 - (unsigned int)-1)
            SessionID _lastSessionID = 0;//session ID sequence. range  [0 - __MIDDLE_SEGMENT_VALUE)
            SessionID _lastConnectID = 0;//connect ID sequence. range  [__MIDDLE_SEGMENT_VALUE - -1)

            //!存储当前的连入连出的session信息和accept监听器信息.
            std::unordered_map<AccepterID, TcpAcceptPtr> _mapAccepterPtr;
            std::unordered_map<SessionID, TcpSessionPtr> _mapTcpSessionPtr;

            //!存储对应的配置信息.
            std::unordered_map<SessionID, std::pair<ConnectConfig, ConnectInfo> > _mapConnectorConfig;
            std::unordered_map<AccepterID, std::pair<ListenConfig, ListenInfo> > _mapAccepterConfig;
        public:
        };



    }
}


#endif
