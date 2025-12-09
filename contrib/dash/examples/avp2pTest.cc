#include <sys/stat.h>  // mkdir()

#include <fstream>

#include "ns3/applications-module.h"  // 引入应用层模块
#include "ns3/config.h"
#include "ns3/core-module.h"          // 核心模块，包含调度、时间等
#include "ns3/flow-monitor-module.h"  // 流量监控模块（这里只是包含，并未使用）
#include "ns3/internet-module.h"      // TCP/IP 协议栈
#include "ns3/multi-av-stream-client.h"
#include "ns3/multi-av-stream-helper.h"
#include "ns3/multi-av-stream-server.h"
#include "ns3/network-module.h"         // 节点、设备、网络基础
#include "ns3/point-to-point-module.h"  // 点对点链路
// #include "ns3/tcp-stream-client.h"       // TcpStreamClient 类
// #include "ns3/tcp-stream-helper.h"       // TCP Stream server/client helper
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/output-stream-wrapper.h"
#include "ns3/tcp-stream-interface.h"    // TCP stream interface
#include "ns3/traffic-control-module.h"  // 队列管理模块（FIFO、RED等）

using namespace ns3;

// 计算单程时延（将总 RTT 简化为 RTT/6）
std::string onelinedelay(uint32_t total_rtt) {
  return std::to_string(int(total_rtt / 6));
}

// 计算 buffer 大小（BDP × times）
std::string bufferpkt(uint32_t total_rtt, uint32_t bd, float times) {
  // BDP = RTT × 带宽
  uint32_t bdp = (total_rtt / 1e3) * (bd * 1e6);  // 单位：bit
  uint32_t n_pkt = bdp / 1446 * times;  // 换算成以 1446 字节为单位的包数
  return std::to_string(n_pkt);
}

// 设置某个节点的 TCP CCA（拥塞控制算法）, 如：TcpBbr / TcpCubic
void SetCCA(Ptr<Node> node, std::string type) {
  Ptr<TcpL4Protocol> tcp = node->GetObject<TcpL4Protocol>();
  tcp->SetAttribute("SocketType", StringValue("ns3::" + type));
}

/*
    2个服务器，服务器1存储视频，服务器2存储音频
    3/4个路由
    1个客户端
    除了客户端以外，均p2p连接(测试用),构造一个多路径传输的网络拓扑结构
    客户端设备为移动设备，客户端设备以5G的蜂窝网络连接到网络中
*/
int main(int argc, char *argv[]) {
  // 数据片的持续时间
  uint64_t segmentDuration = 3000000;
  // 模拟id
  uint32_t simulationId = 103;
  // 客户端总数为1个
  uint32_t numberOfClients = 1;

  // uint16_t ClientId1 = 1;
  // uint16_t ClientId2 = 2;

  std::string adaptationAlgo = "festive";
  std::string videosegmentSizeFilePath = "contrib/dash/videosegmentSizes.txt";
  std::string audiosegmentSizeFilePath = "contrib/dash/audiosegmentSizes.txt";

  // -------------------- 网络参数 --------------------

  // 总 RTT（Round Trip Time，往返时延）
  // 表示从客户端发出一个数据包到收到 ACK 的总时间，单位 ms
  uint32_t m_rtt = 100;  // 总 RTT = 100 ms

  // 链路带宽（Bandwidth），单位 Mbps
  // 这里表示每条链路最大传输速率
  uint32_t m_bd = 15;  // 链路带宽 15 Mbps

  // 缓冲区大小倍数
  // m_buffersize_time = 15，表示队列缓冲区大小是 BDP 的 15 倍
  float m_buffersize_time = 15;

  // TCP 拥塞控制算法类型
  // 客户端默认使用 Cubic
  std::string tcpTypeId = "TcpCubic";

  // 队列类型，使用 FIFO（先进先出）队列
  std::string queueDisc = "FifoQueueDisc";

  // 延迟确认（Delayed ACK）计数
  // TCP 会每收到 delAckCount 个包才发送 ACK
  uint32_t delAckCount = 2;

  // -------------------- 设置全局 TCP 参数 --------------------

  // 在 ns-3 中，QueueDisc 类需要指定完整命名空间
  queueDisc = std::string("ns3::") + queueDisc;

  // 设置默认的 TCP 类型为 TcpCubic ，后面再将部分服务器、客户端指定修改成BBR
  Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                     StringValue("ns3::" + tcpTypeId));

  // 设置发送缓冲区大小（单位字节）
  Config::SetDefault("ns3::TcpSocket::SndBufSize",
                     UintegerValue(4194304));  // 4 MB

  // 设置接收缓冲区大小（单位字节）
  Config::SetDefault("ns3::TcpSocket::RcvBufSize",
                     UintegerValue(6291456));  // 6 MB

  // 设置 TCP 初始拥塞窗口（单位 MSS 包数）
  Config::SetDefault("ns3::TcpSocket::InitialCwnd",
                     UintegerValue(10));  // 10 个报文段

  // 设置延迟确认计数 2个包
  Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(delAckCount));

  // 设置每个 TCP 段的大小（MSS，单位字节）
  Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1448));

  // 设置网卡队列最大长度，这里是 1 个包（"1p"）
  Config::SetDefault("ns3::DropTailQueue<Packet>::MaxSize",
                     QueueSizeValue(QueueSize("1p")));

  // -------------------------------------------------------------------------
  //                          创建 6 个节点
  //     servers(2), routers(3), clients(1)
  // -------------------------------------------------------------------------

  NodeContainer servers;
  NodeContainer clients;
  NodeContainer routers;

  servers.Create(2);  // 2 个服务器
  routers.Create(3);  // 3 个路由器
  clients.Create(1);  // 1 个客户端

  /* -------------------------------------------------------------------------
  //                               链路配置
    2个服务器，服务器1存储视频，服务器2存储音频
    3/4个路由
    1个客户端
    除了客户端以外，均p2p连接(测试用),构造一个多路径传输的网络拓扑结构
    客户端设备为移动设备，客户端设备以5G的蜂窝网络连接到网络中
   -------------------------------------------------------------------------
  */
  std::string delay = onelinedelay(m_rtt) + "ms";  // 单向时延
  std::string bandwidth = std::to_string(m_bd) + "Mbps";
  std::string n_pkt = bufferpkt(m_rtt, m_bd, m_buffersize_time);  // Queue 大小
  // 设置路由器拥塞控制的队列长度
  Config::SetDefault(queueDisc + "::MaxSize",
                     QueueSizeValue(QueueSize(n_pkt + "p")));

  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", StringValue(bandwidth));
  p2p.SetChannelAttribute("Delay", StringValue(delay));

  // 有线链路 server0-r0，server1-r1，r0-r2，r1-r2
  NetDeviceContainer video_to_r0, audio_to_r1, r1_to_r2, r0_to_r2, r2_to_c;
  video_to_r0 = p2p.Install(servers.Get(0), routers.Get(0));
  audio_to_r1 = p2p.Install(servers.Get(1), routers.Get(1));
  r1_to_r2 = p2p.Install(routers.Get(1), routers.Get(2));
  r0_to_r2 = p2p.Install(routers.Get(0), routers.Get(2));
  r2_to_c = p2p.Install(routers.Get(2), clients.Get(0));  // 瓶颈链路

  // -------------------------------------------------------------------------
  //                               安装 TCP/IP 协议栈
  // -------------------------------------------------------------------------

  InternetStackHelper stack;
  stack.InstallAll();

  // UE 通常通过 EPC 分配 IP，但如果要在 UE上运行本地应用，需要安装 stack

  // -------------------------------------------------------------------------
  //                          设置瓶颈队列 (FIFO)
  // -------------------------------------------------------------------------

  // TrafficControlHelper tch;
  // tch.SetRootQueueDisc(queueDisc);
  // tch.Install(r2_to_c);

  // -------------------------------------------------------------------------
  //                               配置 IP 地址
  // -------------------------------------------------------------------------

  Ipv4AddressHelper address;
  // 子网  ， 子网掩码
  address.SetBase("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces1 = address.Assign(video_to_r0);

  address.SetBase("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces2 = address.Assign(audio_to_r1);

  address.SetBase("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces3 = address.Assign(r0_to_r2);

  address.SetBase("10.1.4.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces4 = address.Assign(r1_to_r2);

  address.SetBase("10.1.5.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces5 =
      address.Assign(r2_to_c);  // [0]=r2, [1]=c

  // 生成其他路由（边缘节点到骨干等）
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  // 输出网络拓扑结构

  // -------------------------------------------------------------------------
  //                                创建应用
  // -------------------------------------------------------------------------

  // 视频服务器的端口为9
  uint16_t videoport = 10000;

  // 音频服务器的端口为10
  uint16_t audioport = 10001;

  // ------------------- VideoServer --------------------
  MultiTcpAvStreamServerHelper videoserverHelper;
  ApplicationContainer videoserverApp =
      videoserverHelper.Install(servers.Get(0));
  videoserverApp.Start(Seconds(1.0));

  // ------------------- AudioServer --------------------
  MultiTcpAvStreamServerHelper audioserverHelper;
  ApplicationContainer audioserverApp =
      audioserverHelper.Install(servers.Get(1));
  audioserverApp.Start(Seconds(1.0));

  // -------------------------------------------------------------------------
  // 客户端
  // -------------------------------------------------------------------------

  // 创建一个 vector，用来存放客户端节点和对应自适应算法名称
  // std::pair<Ptr<Node>, std::string>
  // ：第一个元素是节点对象，第二个元素是算法名称
  std::vector<std::pair<Ptr<Node>, std::string>> client;

  // 获取 clients 容器的第一个节点（clients 是之前创建的客户端节点集合）
  NodeContainer::Iterator i = clients.Begin();

  // 将第一个客户端节点和自适应算法名称放入 client 容器
  client.push_back(std::make_pair(*i, adaptationAlgo));
  // *i 表示节点对象，adaptationAlgo 是字符串（如 "festive"）

  // 创建 TCP 流客户端 Helper 对象
  // 参数：服务器 IP 地址、服务器端口 port1
  MultiTcpAvStreamClientHelper clientHelperVideo(
      interfaces1.GetAddress(0), videoport, interfaces2.GetAddress(0),
      audioport);

  // 设置每个 DASH 视频片段的持续时间
  clientHelperVideo.SetAttribute("SegmentDuration",
                                 UintegerValue(segmentDuration));

  // 设置视频片段大小文件路径（DASH 客户端读取每个片段大小）
  clientHelperVideo.SetAttribute("VideoSegmentSizeFilePath",
                                 StringValue(videosegmentSizeFilePath));
  clientHelperVideo.SetAttribute("AudioSegmentSizeFilePath",
                                 StringValue(audiosegmentSizeFilePath));
  // 设置客户端总数量
  clientHelperVideo.SetAttribute("NumberOfClients",
                                 UintegerValue(numberOfClients));

  // 设置仿真 ID，用于日志区分不同仿真
  clientHelperVideo.SetAttribute("SimulationId", UintegerValue(simulationId));
  clientHelperVideo.SetAttribute("StreamSelection", EnumValue(2));
  // 安装客户端应用到节点上
  // Install() 会根据 client
  // 容器中的节点和算法创建对应应用，并返回ApplicationContainer
  ApplicationContainer clientApps1 = clientHelperVideo.Install(client);

  // 为每个客户端应用设置启动时间
  for (uint i = 0; i < clientApps1.GetN(); i++) {
    // 计算启动时间，避免所有客户端同时启动，造成瞬时拥塞
    double startTime = 2.0 + ((i * 3) / 100.0);            // i*0.03 秒的延迟
    clientApps1.Get(i)->SetStartTime(Seconds(startTime));  // 设置应用启动时间
  }

  // -------------------------------------------------------------------------
  //                         创建日志目录
  // -------------------------------------------------------------------------

  // dashLogDirectory 是一个字符串变量，表示仿真日志的根目录路径
  // 比如 dashLogDirectory = "./logs/"
  // c_str() 将 std::string 转为 const char* 类型，因为 mkdir 函数需要 const
  // char* 参数
  const char *mylogsDir = dashLogDirectory.c_str();

  // 使用 mkdir 创建根目录
  // 参数 0777 表示权限：用户/组/其他都有读写执行权限
  mkdir(mylogsDir, 0777);  // 创建根日志目录，如 ./logs/

  // 在根目录下为具体 ABR 自适应算法创建子目录
  // adaptationAlgo 是自适应算法的名字，例如 "festive"
  // 拼接成路径：dashLogDirectory + adaptationAlgo
  std::string algodirstr(dashLogDirectory + adaptationAlgo);

  // 创建 ABR 算法子目录
  // mkdir 只能创建单级目录，如果上级目录不存在会失败
  mkdir(algodirstr.c_str(), 0777);  // 如 ./logs/festive

  // 在算法目录下为不同客户端数量创建子目录
  // numberOfClients 是客户端数量
  // 拼接成路径：dashLogDirectory + adaptationAlgo + "/" + numberOfClients + "/"
  std::string dirstr(dashLogDirectory + adaptationAlgo + "/" +
                     std::to_string(numberOfClients) + "/");

  // 创建最终目录，用于存放该算法在指定客户端数量下的日志
  mkdir(dirstr.c_str(), 0777);  // 如 ./logs/festive/2/

  // 输出网络拓扑路由结构
  std::string routetablefile = dirstr + "/routetable.txt";
  std::ofstream ofs(routetablefile);
  Ptr<OutputStreamWrapper> fileStream = Create<OutputStreamWrapper>(&ofs);
  Ipv4GlobalRoutingHelper::PrintRoutingTableAllAt(Seconds(0.2), fileStream);

  // -------------------------------------------------------------------------
  //                         仿真运行 60 秒
  // -------------------------------------------------------------------------

  Simulator::Stop(Seconds(300));
  Simulator::Run();
  Simulator::Destroy();
  return 0;
}