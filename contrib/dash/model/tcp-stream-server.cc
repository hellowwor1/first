// 包含服务器类的头文件
#include "tcp-stream-server.h"

// 包含 NS3 核心模块
#include <ns3/core-module.h>

// 引入 NS3 地址相关工具函数
#include "ns3/address-utils.h"

// 引入全局变量支持
#include "ns3/global-value.h"

// IPv4 和 IPv6 套接字地址
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

// 引入客户端类
#include "tcp-stream-client.h"

// 声明使用 ns3 命名空间
namespace ns3 {

// 定义日志组件名称
NS_LOG_COMPONENT_DEFINE("TcpStreamServerApplication");

// 确保 TcpStreamServer 类型在 NS3 对象系统中注册
NS_OBJECT_ENSURE_REGISTERED(TcpStreamServer);

// 获取类型标识函数
TypeId TcpStreamServer::GetTypeId(void) {
  // 静态 TypeId，保证只创建一次
  static TypeId tid =
      TypeId("ns3::TcpStreamServer")          // 类型名称
          .SetParent<Application>()           // 指定父类为 Application
          .SetGroupName("Applications")       // 分组名称
          .AddConstructor<TcpStreamServer>()  // 添加默认构造函数
          // 添加属性，指定服务器监听端口
          .AddAttribute("Port", "Port on which we listen for incoming packets.",
                        UintegerValue(9),                                // 默认端口 9
                        MakeUintegerAccessor(&TcpStreamServer::m_port),  // 绑定成员变量
                        MakeUintegerChecker<uint16_t>());                // 类型检查
  return tid;
}

// 构造函数
TcpStreamServer::TcpStreamServer() {
  NS_LOG_FUNCTION(this);  // 记录日志：构造函数被调用
}

// 析构函数,生命周期结束时调用
TcpStreamServer::~TcpStreamServer() {
  NS_LOG_FUNCTION(this);  // 记录日志：析构函数被调用
  m_socket = 0;           // 清空 IPv4 套接字指针
  m_socket6 = 0;          // 清空 IPv6 套接字指针
}

// 释放资源函数
void TcpStreamServer::DoDispose(void) {
  NS_LOG_FUNCTION(this);     // 记录日志
  Application::DoDispose();  // 调用父类释放函数
}

// 应用程序启动函数
void TcpStreamServer::StartApplication(void) {
  NS_LOG_FUNCTION(this);  // 记录日志

  // 如果 IPv4 套接字尚未创建
  if (m_socket == 0) {
    TypeId tid = TypeId::LookupByName("ns3::TcpSocketFactory");  // 获取 TCP 套接字工厂
    m_socket = Socket::CreateSocket(GetNode(), tid);             // 创建套接字
    InetSocketAddress local =
        InetSocketAddress(Ipv4Address::GetAny(), m_port);  // 绑定任意 IPv4 地址和端口
    m_socket->Bind(local);                                 // 绑定套接字
    m_socket->Listen();                                    // 开始监听
  }

  // 如果 IPv6 套接字尚未创建
  if (m_socket6 == 0) {
    TypeId tid = TypeId::LookupByName("ns3::TcpSocketFactory");  // TCP 套接字工厂
    m_socket6 = Socket::CreateSocket(GetNode(), tid);            // 创建套接字
    Inet6SocketAddress local6 =
        Inet6SocketAddress(Ipv6Address::GetAny(), m_port);  // 绑定任意 IPv6 地址
    m_socket6->Bind(local6);                                // 绑定套接字
    m_socket->Listen();  // 开始监听（注意：这里写成 m_socket->Listen()
                         // 是一个小错误，应该是 m_socket6->Listen()）
  }

  // 设置接收连接请求回调
  m_socket->SetAcceptCallback(MakeNullCallback<bool, Ptr<Socket>,
                                               const Address &>(),  // 过滤回调，暂时不检查
                              MakeCallback(&TcpStreamServer::HandleAccept,
                                           this));  // 成功时调用 HandleAccept

  // 设置套接字关闭相关回调
  m_socket->SetCloseCallbacks(
      MakeCallback(&TcpStreamServer::HandlePeerClose, this),   // 对端关闭时调用
      MakeCallback(&TcpStreamServer::HandlePeerError, this));  // 错误时调用
}

// 应用程序停止函数
void TcpStreamServer::StopApplication() {
  NS_LOG_FUNCTION(this);  // 记录日志

  // 如果 IPv4 套接字存在
  if (m_socket != 0) {
    m_socket->Close();                                                  // 关闭套接字
    m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket> >());  // 清空接收回调
  }

  // 如果 IPv6 套接字存在
  if (m_socket6 != 0) {
    m_socket6->Close();                                                  // 关闭套接字
    m_socket6->SetRecvCallback(MakeNullCallback<void, Ptr<Socket> >());  // 清空接收回调
  }
}

// 处理接收到的数据包
void TcpStreamServer::HandleRead(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);  // 记录日志

  Ptr<Packet> packet;  // 数据包指针
  Address from;        // 保存发送者地址

  packet = socket->RecvFrom(from);  // 从套接字接收数据，并获取源地址

  int64_t packetSizeToReturn = GetCommand(packet);  // 解析数据包命令，返回要发送的字节数

  // 为该客户端初始化回调数据
  m_callbackData[from].currentTxBytes = 0;                       // 已发送字节数清零
  m_callbackData[from].packetSizeToReturn = packetSizeToReturn;  // 设置要发送的字节数
  m_callbackData[from].send = true;                              // 标记为需要发送

  HandleSend(socket, socket->GetTxAvailable());  // 尝试发送数据
}

// 处理发送逻辑
void TcpStreamServer::HandleSend(Ptr<Socket> socket, uint32_t txSpace) {
  Address from;
  socket->GetPeerName(from);  // 获取连接客户端地址

  // 检查是否已经发送完数据
  if (m_callbackData[from].currentTxBytes == m_callbackData[from].packetSizeToReturn) {
    m_callbackData[from].currentTxBytes = 0;      // 重置已发送字节数
    m_callbackData[from].packetSizeToReturn = 0;  // 重置要发送字节数
    m_callbackData[from].send = false;            // 标记不再发送
    return;                                       // 退出
  }

  // 如果发送缓冲区有空间并且标记为发送
  if (socket->GetTxAvailable() > 0 && m_callbackData[from].send) {
    int32_t toSend;
    toSend = std::min(socket->GetTxAvailable(),
                      m_callbackData[from].packetSizeToReturn -
                          m_callbackData[from].currentTxBytes);  // 计算实际可发送的字节数

    Ptr<Packet> packet = Create<Packet>(toSend);  // 创建数据包
    int amountSent = socket->Send(packet, 0);     // 发送数据

    if (amountSent > 0) {
      m_callbackData[from].currentTxBytes += amountSent;  // 更新已发送字节数
    } else {
      return;  // 如果发送缓冲区满，则退出，等待回调再次触发
    }
  }
}

// 处理接收到的新连接
void TcpStreamServer::HandleAccept(Ptr<Socket> s, const Address &from) {
  NS_LOG_FUNCTION(this << s << from);  // 记录日志

  callbackData cbd;  // 初始化回调数据
  cbd.currentTxBytes = 0;
  cbd.packetSizeToReturn = 0;
  cbd.send = false;
  m_callbackData[from] = cbd;          // 存储回调数据
  m_connectedClients.push_back(from);  // 添加到已连接客户端列表

  s->SetRecvCallback(MakeCallback(&TcpStreamServer::HandleRead, this));  // 设置接收回调
  s->SetSendCallback(MakeCallback(&TcpStreamServer::HandleSend, this));  // 设置发送回调
}

// 处理客户端关闭连接
void TcpStreamServer::HandlePeerClose(Ptr<Socket> socket) {
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
void TcpStreamServer::HandlePeerError(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);  // 记录日志
}

// 解析数据包命令，获取要发送的字节数
int64_t TcpStreamServer::GetCommand(Ptr<Packet> packet) {
  int64_t packetSizeToReturn;                        // 需要返回的字节数
  uint8_t *buffer = new uint8_t[packet->GetSize()];  // 为数据分配缓冲区
  packet->CopyData(buffer, packet->GetSize());       // 复制数据到缓冲区

  // std::cout << "2: ";
  // std::cout << buffer << std::endl;

  std::stringstream ss;
  ss << buffer;  // 将缓冲区内容转换为字符串
  std::string str;
  ss >> str;  // 读取字符串

  // std::cout << "1: " + str << std::endl;

  std::stringstream convert(str);
  // 调试
  //  std::cout << str << std::endl;
  convert >> packetSizeToReturn;  // 将字符串转换为整数
  return packetSizeToReturn;      // 返回字节数
}

}  // namespace ns3