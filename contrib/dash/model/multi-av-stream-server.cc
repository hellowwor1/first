// 包含多TCP流AV服务器的头文件
#include "multi-av-stream-server.h"

// 包含NS3核心模块
#include <ns3/core-module.h>

// 引入NS3地址相关工具函数
#include "ns3/address-utils.h"

// IPv4和IPv6套接字地址
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv6-address.h"

// 日志系统
#include "ns3/log.h"

// 时间相关
#include "ns3/nstime.h"

// 数据包相关
#include "ns3/packet.h"

// 仿真器
#include "ns3/simulator.h"

// 套接字工厂和套接字
#include "ns3/socket-factory.h"
#include "ns3/socket.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/tcp-socket.h"

// 跟踪源访问
#include "ns3/trace-source-accessor.h"

// 无符号整数类型支持
#include "ns3/uinteger.h"

// 字符串流支持
#include <sstream>

// 声明使用ns3命名空间
namespace ns3 {

// 定义日志组件名称
NS_LOG_COMPONENT_DEFINE("MultiTcpAvStreamServerApplication");

// 确保MultiTcpAvStreamServer类型在NS3对象系统中注册
NS_OBJECT_ENSURE_REGISTERED(MultiTcpAvStreamServer);

// 获取类型标识函数
TypeId MultiTcpAvStreamServer::GetTypeId(void) {
  // 静态TypeId，保证只创建一次
  static TypeId tid =
      TypeId("ns3::MultiTcpAvStreamServer")          // 类型名称
          .SetParent<Application>()                  // 指定父类为Application
          .SetGroupName("Applications")              // 分组名称
          .AddConstructor<MultiTcpAvStreamServer>()  // 添加默认构造函数
          // 添加视频端口属性
          .AddAttribute(
              "VideoPort", "Port on which we listen for video stream requests.",
              UintegerValue(10000),  // 默认视频端口10000
              MakeUintegerAccessor(&MultiTcpAvStreamServer::m_videoPort),
              MakeUintegerChecker<uint16_t>())
          // 添加音频端口属性
          .AddAttribute(
              "AudioPort", "Port on which we listen for audio stream requests.",
              UintegerValue(10001),  // 默认音频端口10001
              MakeUintegerAccessor(&MultiTcpAvStreamServer::m_audioPort),
              MakeUintegerChecker<uint16_t>());
  return tid;
}

// 构造函数
MultiTcpAvStreamServer::MultiTcpAvStreamServer()
    : m_videoPort(10000),   // 初始化视频端口
      m_audioPort(10001) {  // 初始化音频端口
  NS_LOG_FUNCTION(this);    // 记录日志：构造函数被调用
}

// 析构函数，生命周期结束时调用
MultiTcpAvStreamServer::~MultiTcpAvStreamServer() {
  NS_LOG_FUNCTION(this);  // 记录日志：析构函数被调用
  m_videoSocket = 0;      // 清空视频IPv4套接字指针
  m_videoSocket6 = 0;     // 清空视频IPv6套接字指针
  m_audioSocket = 0;      // 清空音频IPv4套接字指针
  m_audioSocket6 = 0;     // 清空音频IPv6套接字指针
}

// 设置视频流端口
void MultiTcpAvStreamServer::SetVideoPort(uint16_t port) {
  NS_LOG_FUNCTION(this << port);  // 记录日志
  m_videoPort = port;             // 更新视频端口
}

// 设置音频流端口
void MultiTcpAvStreamServer::SetAudioPort(uint16_t port) {
  NS_LOG_FUNCTION(this << port);  // 记录日志
  m_audioPort = port;             // 更新音频端口
}

// 获取视频流端口
uint16_t MultiTcpAvStreamServer::GetVideoPort(void) const {
  NS_LOG_FUNCTION(this);  // 记录日志
  return m_videoPort;     // 返回视频端口
}

// 获取音频流端口
uint16_t MultiTcpAvStreamServer::GetAudioPort(void) const {
  NS_LOG_FUNCTION(this);  // 记录日志
  return m_audioPort;     // 返回音频端口
}

// 释放资源函数
void MultiTcpAvStreamServer::DoDispose(void) {
  NS_LOG_FUNCTION(this);     // 记录日志
  Application::DoDispose();  // 调用父类释放函数
}

// 应用程序启动函数
void MultiTcpAvStreamServer::StartApplication(void) {
  NS_LOG_FUNCTION(this);  // 记录日志

  // 创建并初始化视频流套接字（IPv4）
  if (m_videoSocket == 0) {
    TypeId tid =
        TypeId::LookupByName("ns3::TcpSocketFactory");     // 获取TCP套接字工厂
    m_videoSocket = Socket::CreateSocket(GetNode(), tid);  // 创建套接字
    InetSocketAddress local =
        InetSocketAddress(Ipv4Address::GetAny(), m_videoPort);
    if (m_videoSocket->Bind(local) == -1) {           // 绑定套接字到指定端口
      NS_FATAL_ERROR("Failed to bind video socket");  // 绑定失败则报错
    }
    m_videoSocket->Listen();  // 开始监听连接请求
    NS_LOG_INFO("Video server listening on port " << m_videoPort);  // 记录日志
  }

  // 创建并初始化视频流套接字（IPv6）
  if (m_videoSocket6 == 0) {
    TypeId tid = TypeId::LookupByName("ns3::TcpSocketFactory");
    m_videoSocket6 = Socket::CreateSocket(GetNode(), tid);
    Inet6SocketAddress local6 =
        Inet6SocketAddress(Ipv6Address::GetAny(), m_videoPort);
    if (m_videoSocket6->Bind(local6) == -1) {
      NS_FATAL_ERROR("Failed to bind video socket6");
    }
    m_videoSocket6->Listen();
    NS_LOG_INFO("Video server listening on port " << m_videoPort);  // 记录日志
  }

  // 创建并初始化音频流套接字（IPv4）
  if (m_audioSocket == 0) {
    TypeId tid = TypeId::LookupByName("ns3::TcpSocketFactory");
    m_audioSocket = Socket::CreateSocket(GetNode(), tid);
    InetSocketAddress local =
        InetSocketAddress(Ipv4Address::GetAny(), m_audioPort);
    if (m_audioSocket->Bind(local) == -1) {
      NS_FATAL_ERROR("Failed to bind audio socket");
    }
    m_audioSocket->Listen();
    NS_LOG_INFO("Audio server listening on port " << m_audioPort);
  }

  // 创建并初始化音频流套接字（IPv6）
  if (m_audioSocket6 == 0) {
    TypeId tid = TypeId::LookupByName("ns3::TcpSocketFactory");
    m_audioSocket6 = Socket::CreateSocket(GetNode(), tid);
    Inet6SocketAddress local6 =
        Inet6SocketAddress(Ipv6Address::GetAny(), m_audioPort);
    if (m_audioSocket6->Bind(local6) == -1) {
      NS_FATAL_ERROR("Failed to bind audio socket6");
    }
    m_audioSocket6->Listen();
    NS_LOG_INFO("audio server listening on port " << m_audioPort);  // 记录日志
  }

  // 设置视频套接字连接接受回调
  m_videoSocket->SetAcceptCallback(
      MakeNullCallback<bool, Ptr<Socket>, const Address&>(),  // 无条件接受连接
      MakeCallback(&MultiTcpAvStreamServer::HandleAccept,
                   this));  //  成功时调用 HandleAccept

  m_videoSocket6->SetAcceptCallback(
      MakeNullCallback<bool, Ptr<Socket>, const Address&>(),
      MakeCallback(&MultiTcpAvStreamServer::HandleAccept, this));

  // 设置音频套接字连接接受回调
  m_audioSocket->SetAcceptCallback(
      MakeNullCallback<bool, Ptr<Socket>, const Address&>(),
      MakeCallback(&MultiTcpAvStreamServer::HandleAccept, this));

  m_audioSocket6->SetAcceptCallback(
      MakeNullCallback<bool, Ptr<Socket>, const Address&>(),
      MakeCallback(&MultiTcpAvStreamServer::HandleAccept, this));

  // 设置套接字关闭相关回调
  m_videoSocket->SetCloseCallbacks(
      MakeCallback(&MultiTcpAvStreamServer::HandlePeerClose, this),
      MakeCallback(&MultiTcpAvStreamServer::HandlePeerError, this));

  m_videoSocket6->SetCloseCallbacks(
      MakeCallback(&MultiTcpAvStreamServer::HandlePeerClose, this),
      MakeCallback(&MultiTcpAvStreamServer::HandlePeerError, this));

  m_audioSocket->SetCloseCallbacks(
      MakeCallback(&MultiTcpAvStreamServer::HandlePeerClose, this),
      MakeCallback(&MultiTcpAvStreamServer::HandlePeerError, this));

  m_audioSocket6->SetCloseCallbacks(
      MakeCallback(&MultiTcpAvStreamServer::HandlePeerClose, this),
      MakeCallback(&MultiTcpAvStreamServer::HandlePeerError, this));
}

// 应用程序停止函数
void MultiTcpAvStreamServer::StopApplication(void) {
  NS_LOG_FUNCTION(this);  // 记录日志

  // 关闭视频套接字并清空回调
  if (m_videoSocket != 0) {
    m_videoSocket->Close();
    m_videoSocket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
  }

  if (m_videoSocket6 != 0) {
    m_videoSocket6->Close();
    m_videoSocket6->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
  }

  // 关闭音频套接字并清空回调
  if (m_audioSocket != 0) {
    m_audioSocket->Close();
    m_audioSocket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
  }

  if (m_audioSocket6 != 0) {
    m_audioSocket6->Close();
    m_audioSocket6->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
  }
}

// 处理收到的数据包
void MultiTcpAvStreamServer::HandleRead(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);  // 记录日志

  Ptr<Packet> packet;  // 数据包指针
  Address from;        // 客户端地址

  // 从套接字接收数据包，并获取源地址
  packet = socket->RecvFrom(from);

  // 解析客户端请求，获取请求的字节数（纯数字）
  int64_t packetSizeToReturn = ParseCommand(packet);

  // 获取本地端口以确定流类型
  Address localAddress;
  socket->GetSockName(localAddress);
  InetSocketAddress inetLocal = InetSocketAddress::ConvertFrom(localAddress);
  // 根据端口确定流类型
  std::string streamType = GetStreamTypeFromPort(inetLocal.GetPort());

  // 为该流初始化回调数据
  m_callbackDataMap[from].currentTxBytes = 0;  // 已发送字节数清零
  m_callbackDataMap[from].packetSizeToReturn =
      packetSizeToReturn;                           // 设置要发送的总字节数
  m_callbackDataMap[from].send = true;              // 标记为需要发送
  m_callbackDataMap[from].streamType = streamType;  // 记录流类型

  // 尝试发送数据
  HandleSend(socket, socket->GetTxAvailable());
}

// 处理发送逻辑
void MultiTcpAvStreamServer::HandleSend(Ptr<Socket> socket, uint32_t txSpace) {
  NS_LOG_FUNCTION(this << socket << txSpace);  // 记录日志

  Address from;
  socket->GetPeerName(from);  // 获取客户端地址

  // 检查是否已经发送完数据
  if (m_callbackDataMap[from].currentTxBytes ==
      m_callbackDataMap[from].packetSizeToReturn) {
    m_callbackDataMap[from].currentTxBytes = 0;      // 重置已发送字节数
    m_callbackDataMap[from].packetSizeToReturn = 0;  // 重置要发送字节数
    m_callbackDataMap[from].send = false;            // 标记不再发送
    return;                                          // 退出
  }

  // 如果发送缓冲区有空间并且标记为发送
  if (socket->GetTxAvailable() > 0 && m_callbackDataMap[from].send) {
    // 计算实际可发送的字节数
    uint32_t toSend = std::min(socket->GetTxAvailable(),
                               m_callbackDataMap[from].packetSizeToReturn -
                                   m_callbackDataMap[from].currentTxBytes);

    // 创建数据包并发送
    Ptr<Packet> packet = Create<Packet>(toSend);
    int amountSent = socket->Send(packet, 0);

    if (amountSent > 0) {
      m_callbackDataMap[from].currentTxBytes += amountSent;  // 更新已发送字节数
    } else {
      return;  // 如果发送缓冲区满，则退出，等待回调再次触发
    }
  }
}

// 处理接收到的新连接
void MultiTcpAvStreamServer::HandleAccept(Ptr<Socket> s, const Address& from) {
  NS_LOG_FUNCTION(this << s << from);  // 记录日志

  // 获取本地端口以确定流类型
  Address localAddress;
  s->GetSockName(localAddress);
  InetSocketAddress inetLocal = InetSocketAddress::ConvertFrom(localAddress);
  uint16_t localPort = inetLocal.GetPort();

  // 根据端口确定流类型
  std::string streamType = GetStreamTypeFromPort(localPort);

  // 初始化回调数据结构
  MultiTcpAvCallbackData cbd;
  cbd.currentTxBytes = 0;
  cbd.packetSizeToReturn = 0;
  cbd.send = false;
  cbd.streamType = streamType;

  // 存储回调数据
  m_callbackDataMap[from] = cbd;

  // 添加到已连接客户端列表
  m_connectedClients.push_back(from);

  // 设置接收回调
  s->SetRecvCallback(MakeCallback(&MultiTcpAvStreamServer::HandleRead, this));

  // 设置发送回调
  s->SetSendCallback(MakeCallback(&MultiTcpAvStreamServer::HandleSend, this));
}

// 处理客户端关闭连接
void MultiTcpAvStreamServer::HandlePeerClose(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);  // 记录日志

  Address from;
  socket->GetPeerName(from);  // 获取客户端地址

  // 从已连接列表中删除客户端
  for (std::vector<Address>::iterator it = m_connectedClients.begin();
       it != m_connectedClients.end(); ++it) {
    if (*it == from) {
      m_connectedClients.erase(it);  // 删除客户端
      if (m_connectedClients.size() == 0) {
        Simulator::Stop();  // 如果没有客户端，停止仿真
      }
      return;
    }
  }
}

// 处理客户端出现错误
void MultiTcpAvStreamServer::HandlePeerError(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);  // 记录日志
}

// 解析客户端请求命令
int64_t MultiTcpAvStreamServer::ParseCommand(Ptr<Packet> packet) {
  NS_LOG_FUNCTION(this << packet);  // 记录日志
  int64_t packetSizeToReturn;
  // 分配缓冲区并复制数据
  uint8_t* buffer = new uint8_t[packet->GetSize()];
  packet->CopyData(buffer, packet->GetSize());

  std::stringstream ss;
  ss << buffer;  // 将缓冲区内容转换为字符串
  std::string str;
  ss >> str;  // 读取字符串

  std::stringstream convert(str);

  convert >> packetSizeToReturn;  // 将字符串转换为整数
  return packetSizeToReturn;      // 返回字节数
}

// 从端口号获取流类型
std::string MultiTcpAvStreamServer::GetStreamTypeFromPort(uint16_t localPort) {
  NS_LOG_FUNCTION(this << localPort);  // 记录日志

  if (localPort == m_videoPort) {
    return "video";
  } else if (localPort == m_audioPort) {
    return "audio";
  } else {
    NS_LOG_WARN("Unknown port " << localPort << ", defaulting to 'unknown'");
    return "unknown";
  }
}

}  // namespace ns3