#include <sys/stat.h>  // mkdir()

#include <fstream>
#include <unordered_map>

#include "../model/OwdManager.h"
#include "flowstate.h"
#include "ns3/applications-module.h"  // 引入应用层模块
#include "ns3/bulk-send-helper.h"
#include "ns3/config.h"
#include "ns3/core-module.h"  // 核心模块，包含调度、时间等
#include "ns3/drop-tail-queue.h"
#include "ns3/flow-monitor-module.h"  // 流量监控模块（这里只是包含，并未使用）
#include "ns3/internet-module.h"      // TCP/IP 协议栈
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-interface-container.h"
#include "ns3/multi-av-stream-client.h"
#include "ns3/multi-av-stream-helper.h"
#include "ns3/multi-av-stream-server.h"
#include "ns3/network-module.h"  // 节点、设备、网络基础
#include "ns3/output-stream-wrapper.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/point-to-point-module.h"  // 点对点链路
#include "ns3/point-to-point-net-device.h"
#include "ns3/queue-disc.h"
#include "ns3/queue.h"
#include "ns3/tcp-header.h"
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
std::vector<std::string> g_bwTrace;

// 读取 trace 链路带宽文件 每秒变化瓶颈链路的带宽
void LoadBandwidthTrace(const std::string &filename) {
  std::ifstream infile(filename);
  std::string line;

  if (!infile.is_open()) {
    NS_FATAL_ERROR("Cannot open bandwidth trace file: " << filename);
  }

  while (std::getline(infile, line)) {
    if (!line.empty()) {
      g_bwTrace.push_back(line);
    }
  }

  infile.close();

  NS_LOG_UNCOND("Loaded " << g_bwTrace.size() << " bandwidth samples");
}

void UpdateLinkBandwidth(Ptr<PointToPointNetDevice> dev, uint32_t index) {
  if (index >= g_bwTrace.size()) {
    return;  // 文件读完就停止更新
  }

  std::string bw = g_bwTrace[index];

  dev->SetDataRate(DataRate(bw));

  // NS_LOG_UNCOND("Time " << Simulator::Now().GetSeconds()
  //                       << "s, set link bandwidth = " << bw);

  // 1 秒后更新下一条
  Simulator::Schedule(Seconds(1.0), &UpdateLinkBandwidth, dev, index + 1);
}

static Ptr<MultiTcpAvStreamClient> g_clientApp;
// 在客户端ip层读取包头检测CE标志
void ClientIpv4RxTrace(Ptr<const Packet> packet, Ptr<Ipv4> ipv4,
                       uint32_t interface) {
  Ptr<Packet> pkt = packet->Copy();

  // 解析 IP 头
  Ipv4Header ip;
  if (!pkt->RemoveHeader(ip)) return;

  // 只关心 TCP
  if (ip.GetProtocol() != 6) return;

  // 解析 TCP 头
  TcpHeader tcp;
  if (!pkt->PeekHeader(tcp)) return;

  // 检查 ECN
  // NS_LOG_UNCOND("准备检测ECN...  time:"
  //               << Simulator::Now().GetSeconds() << "s, src IP: "
  //               << ip.GetSource() << ", src port: " << tcp.GetSourcePort());
  if (ip.GetEcn() == Ipv4Header::ECN_CE) {
    // double timeNow = Simulator::Now().GetMicroSeconds() / (double)1000000;
    // NS_LOG_UNCOND("CE标志成功检测到! "
    //               << " 时间：" << timeNow
    //               << " flow src IP : " << ip.GetSource()  // IP 来自
    //               Ipv4Header
    //               << ", src port: "
    //               << tcp.GetSourcePort());  // 源端口来自 TcpHeader
    if (g_clientApp) {
      g_clientApp->NotifyEcn(ip.GetSource(), tcp.GetSourcePort(),
                             Simulator::Now());
    }
  }
}

// 每秒统计
static uint64_t g_videoBytes = 0;
static uint64_t g_audioBytes = 0;

// 记录时间
static double g_lastTime = 0.0;
static const uint16_t VIDEO_SRC_PORT = 10;
static const uint16_t AUDIO_SRC_PORT = 11;
static uint32_t g_bottleneckIf = 0;
static std::ofstream g_bwShareFile;
Ipv4Address videoAddress;
Ipv4Address audioAddress;
uint32_t videoId, audioId, clientId;

void BottleneckIpv4TxTrace(Ptr<const Packet> packet, Ptr<Ipv4> ipv4,
                           uint32_t interface) {
  // 1. 只统计瓶颈接口（r2 -> client）
  if (interface != g_bottleneckIf) {
    return;
  }

  // 2. 拷贝 packet（因为要解析头）
  Ptr<Packet> pkt = packet->Copy();

  Ipv4Header ipHeader;
  TcpHeader tcpHeader;

  // 3. 解析 IP 头（Ipv4::Tx 一定能解析成功，但保守起见）
  if (!pkt->RemoveHeader(ipHeader)) {
    return;
  }

  // 4. 只统计 TCP
  if (ipHeader.GetProtocol() != 6) {
    return;
  }

  // 5. 解析 TCP 头
  if (!pkt->PeekHeader(tcpHeader)) {
    return;
  }

  Ipv4Address srcIp = ipHeader.GetSource();
  uint32_t pktSize = packet->GetSize();  // 含 IP/TCP 头

  // 3. 按 src IP 区分 flow（与 QueueDisc 完全一致）
  if (srcIp == videoAddress) {
    g_videoBytes += pktSize;
  } else if (srcIp == audioAddress) {
    g_audioBytes += pktSize;
  }
}
void BottleneckQueueDequeueTrace(Ptr<const QueueDiscItem> item) {
  static bool once = false;
  if (!once) {
    NS_LOG_UNCOND("QueueDisc Dequeue works!");
    once = true;
  }
  // 1. 必须先 dynamic_cast
  Ptr<const Ipv4QueueDiscItem> ipv4Item =
      DynamicCast<const Ipv4QueueDiscItem>(item);

  if (!ipv4Item) {
    return;  // 不是 IPv4 包，直接忽略
  }

  Ptr<Packet> pkt = ipv4Item->GetPacket()->Copy();

  Ipv4Header ipHeader;
  TcpHeader tcpHeader;

  // 2. 解析 IP 头（这里一定能成功）
  pkt->RemoveHeader(ipHeader);

  if (ipHeader.GetProtocol() != 6) {
    return;  // 只统计 TCP
  }

  // 3. Peek TCP 头
  if (!pkt->PeekHeader(tcpHeader)) {
    return;
  }

  uint16_t srcPort = tcpHeader.GetSourcePort();

  // 4. 用 QueueDiscItem 的 size
  uint32_t pktSize = ipv4Item->GetSize();

  if (srcPort == VIDEO_SRC_PORT) {
    g_videoBytes += pktSize;
  } else if (srcPort == AUDIO_SRC_PORT) {
    g_audioBytes += pktSize;
  }
}

void ReportBandwidthShare() {
  double now = Simulator::Now().GetSeconds();
  double interval = now - g_lastTime;

  uint64_t totalBytes = g_videoBytes + g_audioBytes;
  NS_LOG_UNCOND("ReportBandwidthShare     now:"
                << now << "  interval:" << interval << "  totalBytes:"
                << totalBytes << " videoBytes:" << g_videoBytes
                << " audioBytes:" << g_audioBytes);
  if (totalBytes > 0 && interval > 0) {
    double videoShare = (double)g_videoBytes / totalBytes;
    double audioShare = (double)g_audioBytes / totalBytes;

    double videoMbps = g_videoBytes * 8.0 / interval / 1e6;
    double audioMbps = g_audioBytes * 8.0 / interval / 1e6;

    g_bwShareFile << now << "\t" << videoShare << "\t" << audioShare << "\t"
                  << videoMbps << "\t" << audioMbps << std::endl;
    g_bwShareFile.flush();
  }

  // 清零，进入下一个 1s 窗口
  g_videoBytes = 0;
  g_audioBytes = 0;
  g_lastTime = now;

  Simulator::Schedule(Seconds(1.0), &ReportBandwidthShare);
}

// 统计瓶颈队列处排队的数据包信息
void PrintQueueSize(Ptr<QueueDisc> q) {
  uint32_t nPackets = q->GetNPackets();
  uint32_t nBytes = q->GetNBytes();

  NS_LOG_UNCOND(Simulator::Now().GetSeconds()
                << "s  Queue packets= " << nPackets << " bytes=" << nBytes);

  Simulator::Schedule(MilliSeconds(100), &PrintQueueSize, q);
}

static const uint64_t VIDEO_FLOW = 1;
static const uint64_t AUDIO_FLOW = 2;
std::map<uint64_t, FlowQueueStat> m_flowStats;

uint64_t GetFlowId(Ptr<const QueueDiscItem> item) {
  Ptr<const Ipv4QueueDiscItem> ipItem =
      DynamicCast<const Ipv4QueueDiscItem>(item);

  if (!ipItem) return 9999;

  const Ipv4Header &ip = ipItem->GetHeader();
  Ipv4Address src = ip.GetSource();

  uint64_t fid = 9999;
  if (src == videoAddress)
    fid = VIDEO_FLOW;
  else if (src == audioAddress)
    fid = AUDIO_FLOW;
  return fid;
}

void OnEnqueue(Ptr<const QueueDiscItem> item) {
  uint64_t fid = GetFlowId(item);
  m_flowStats[fid].queuedPackets++;
  m_flowStats[fid].enqueuedPackets++;
}

void OnDequeue(Ptr<const QueueDiscItem> item) {
  uint64_t fid = GetFlowId(item);
  if (m_flowStats[fid].queuedPackets > 0) {
    m_flowStats[fid].queuedPackets--;
    m_flowStats[fid].dequeuedPackets++;
  }
}

void OnDrop(Ptr<const QueueDiscItem> item) {
  uint64_t fid = GetFlowId(item);
  m_flowStats[fid].droppedPackets++;
}

void PeriodicQueueStat() {
  uint32_t totalEnQueue = 0, totalQueued = 0, totalDropped = 0,
           totalDequeued = 0;
  for (auto &kv : m_flowStats) {
    totalEnQueue += kv.second.enqueuedPackets;
    totalQueued += kv.second.queuedPackets;
    totalDropped += kv.second.droppedPackets;
    totalDequeued += kv.second.dequeuedPackets;
  }

  double now = Simulator::Now().GetSeconds();

  for (auto &kv : m_flowStats) {
    double Queuedratio = 0.0, Dequeuedratio = 0.0, Droppedratio = 0.0,
           Enqueuedratio = 0.0;
    if (totalQueued > 0) {
      Queuedratio = double(kv.second.queuedPackets) / totalQueued;
    }
    if (totalDequeued > 0) {
      Dequeuedratio = double(kv.second.dequeuedPackets) / totalDequeued;
    }
    if (totalDropped > 0) {
      Droppedratio = double(kv.second.droppedPackets) / totalDropped;
    }
    if (totalEnQueue > 0) {
      Enqueuedratio = double(kv.second.enqueuedPackets) / totalEnQueue;
    }
    std::string flowName = (kv.first == VIDEO_FLOW) ? "video" : "audio";
    NS_LOG_UNCOND("Time " << now << " flowId=" << kv.first << "  " << flowName
                          << " enqueue= " << kv.second.enqueuedPackets
                          << " enqueue_ratio=" << Enqueuedratio
                          << " queued=" << kv.second.queuedPackets
                          << " queued_ratio=" << Queuedratio << "\n"
                          << " dequeued=" << kv.second.dequeuedPackets
                          << " dequeued_ratio=" << Dequeuedratio
                          << " dropped=" << kv.second.droppedPackets
                          << " dropped_ratio=" << Droppedratio);
  }

  // 重置周期统计（可选）
  for (auto &kv : m_flowStats) {
    kv.second.droppedPackets = 0;
    kv.second.dequeuedPackets = 0;
    kv.second.enqueuedPackets = 0;
  }

  Simulator::Schedule(MilliSeconds(350), &PeriodicQueueStat);
}

void OnNetDeviceQueueDrop(Ptr<const Packet> p) {
  NS_LOG_UNCOND("NetDevice TxQueue DROP, size="
                << p->GetSize() << " at time "
                << Simulator::Now().GetSeconds());
}

void ComputePacketLossRate(Ptr<FlowMonitor> fm) {
  // 获取统计结果
  FlowMonitor::FlowStatsContainer flowStats = fm->GetFlowStats();

  uint32_t totalSentPackets = 0;
  uint32_t totalReceivedPackets = 0;

  // 计算每个流的数据包发送和接收数量
  for (auto it : flowStats) {
    if (it.second.txPackets > 0) {
      totalSentPackets += it.second.txPackets;
      totalReceivedPackets += it.second.rxPackets;
    }
  }

  // 计算丢包率
  uint32_t lostPackets = totalSentPackets - totalReceivedPackets;
  double packetLossRate =
      static_cast<double>(lostPackets) / totalSentPackets * 100.0;

  NS_LOG_UNCOND("Total Sent Packets: " << totalSentPackets);
  NS_LOG_UNCOND("Total Received Packets: " << totalReceivedPackets);
  NS_LOG_UNCOND("Lost Packets: " << lostPackets);
  NS_LOG_UNCOND("Packet Loss Rate: " << packetLossRate << "%");
}

// static void VideoBytesInFlightTrace(std::string context, uint32_t oldValue,
//                                     uint32_t newValue) {
//   NS_LOG_UNCOND(Simulator::Now().GetSeconds()
//                 << "s Video BytesInFlight: " << oldValue / 1448 << " -> "
//                 << newValue / 1448);
// }

// static void AudioBytesInFlightTrace(std::string context, uint32_t oldValue,
//                                     uint32_t newValue) {
//   NS_LOG_UNCOND(Simulator::Now().GetSeconds()
//                 << "s Audio BytesInFlight: " << oldValue / 1448 << " -> "
//                 << newValue / 1448);
// }
// static void
// VideoCwndTrace(std::string context, uint32_t oldValue, uint32_t newValue) {
//   NS_LOG_UNCOND(Simulator::Now().GetSeconds()
//                 << "s Video Cwnd: " << oldValue / 1448 << " -> "
//                 << newValue / 1448);
// }
// static void
// AudioCwndTrace(std::string context, uint32_t oldValue, uint32_t newValue) {
//   NS_LOG_UNCOND(Simulator::Now().GetSeconds()
//                 << "s Audio Cwnd: " << oldValue / 1448 << " -> "
//                 << newValue / 1448);
// }
// static void VideoRTTTrace(std::string context, Time oldValue, Time newValue)
// {
//   NS_LOG_UNCOND(Simulator::Now().GetSeconds()
//                 << "s Video RTT: " << oldValue.GetMilliSeconds() << "ms -> "
//                 << newValue.GetMilliSeconds() << "ms");
// }
// static void AudioRTTTrace(std::string context, Time oldValue, Time newValue)
// {
//   NS_LOG_UNCOND(Simulator::Now().GetSeconds()
//                 << "s Audio RTT: " << oldValue.GetMilliSeconds() << "ms -> "
//                 << newValue.GetMilliSeconds() << "ms");
// }

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
  // 103 模拟 r0_r2与r2_c这2条链路上面有拥塞情况
  // 104 模拟畅通无阻
  // 105 模拟有大量随机启动随机结束的背景流量(不同步的问题以及作出来)
  // 106 2个流之间的竞争问题作出来(使用时变链路带宽,不用随机的背景流量)
  // 107
  // 更改了瓶颈处的队列，重新做出问题;加入ECN标志，在客户端侧保持2个流的CE标志;用Rtt检测共享瓶颈;加入解决方案，新设计一个sabba的ABR算法
  uint32_t simulationId = 108;
  // 客户端总数为1个
  uint32_t numberOfClients = 1;

  // uint16_t ClientId1 = 1;
  // uint16_t ClientId2 = 2;

  // std::string video_adaptationAlgo = "sabba";
  // std::string audio_adaptationAlgo = "sabba";
  std::string video_adaptationAlgo = "abr";
  std::string audio_adaptationAlgo = "abr";
  // std::string video_adaptationAlgo = "festive";
  // std::string audio_adaptationAlgo = "festive";
  std::string videoDir = "video_" + video_adaptationAlgo;
  std::string audioDir = "audio_" + audio_adaptationAlgo;

  std::string videosegmentSizeFilePath = "contrib/dash/segments/videoSizes.txt";
  std::string audiosegmentSizeFilePath = "contrib/dash/segments/audioSizes.txt";

  // std::string videosegmentSizeFilePath =
  //     "contrib/dash/segments/videoSizesV2.txt";
  // std::string audiosegmentSizeFilePath =
  //     "contrib/dash/segments/audioSizesV2.txt";

  // -------------------------------------------------------------------------
  //                         创建日志目录
  // -------------------------------------------------------------------------

  // 初始化日志文件目录
  std::string basePrefix = dashLogDirectory + "/sim" +
                           std::to_string(simulationId) + "_" + "cl" +
                           std::to_string(numberOfClients);
  std::string videoPrefix = basePrefix + "/" + videoDir;
  std::string audioPrefix = basePrefix + "/" + audioDir;
  // c_str() 将 std::string 转为 const char* 类型，因为 mkdir 函数需要 const
  // char* 参数
  const char *mylogsDir = dashLogDirectory.c_str();
  // 参数 0777 表示权限：用户/组/其他都有读写执行权限
  mkdir(mylogsDir, 0777);  // 创建根日志目录 /dash-log-files
  mkdir(basePrefix.c_str(), 0777);
  mkdir(videoPrefix.c_str(), 0777);
  mkdir(audioPrefix.c_str(), 0777);

  g_bwShareFile.open(basePrefix + "/bottleneck_bandwidth_share.txt",
                     std::ios::out | std::ios::trunc);

  if (!g_bwShareFile.is_open()) {
    NS_FATAL_ERROR("Cannot open output file!");
  }
  // 写表头
  g_bwShareFile << "Time\t"
                << "VideoShare\t"
                << "AudioShare\t"
                << "VideoMbps\t"
                << "AudioMbps" << std::endl;

  // -------------------- 网络参数 --------------------

  // 总 RTT（Round Trip Time，往返时延）
  // 表示从客户端发出一个数据包到收到 ACK 的总时间，单位 ms
  uint32_t m_rtt = 45;  // 总 RTT

  // 链路带宽（Bandwidth），单位 Mbps
  // 这里表示每条链路最大传输速率
  uint32_t m_bd_v1 = 20;  // 链路带宽 20 Mbps

  // 缓冲区大小倍数
  // m_buffersize_time = 15，表示队列缓冲区大小是 BDP 的 15 倍
  float m_buffersize_time = 15;

  // TCP 拥塞控制算法类型
  // 客户端默认使用 Cubic
  std::string tcpTypeId = "TcpCubic";
  // std::string tcpTypeId = "TcpBbr";
  // 队列类型，使用 FIFO（先进先出）队列
  std::string queueDisc = "FifoQueueDisc";

  // 延迟确认（Delayed ACK）计数
  // TCP 会每收到 delAckCount 个包才发送 ACK
  uint32_t delAckCount = 2;

  // -------------------- 设置全局 TCP 参数 --------------------

  // 在 ns-3 中，QueueDisc 类需要指定完整命名空间
  queueDisc = std::string("ns3::") + queueDisc;

  // 设置默认的 TCP 类型为 TcpCubic
  Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                     StringValue("ns3::" + tcpTypeId));

  // 设置发送缓冲区大小（单位字节）
  // Config::SetDefault("ns3::TcpSocket::SndBufSize",
  //                    UintegerValue(4194304 * 3));  // 4 MB
  Config::SetDefault("ns3::TcpSocket::SndBufSize",
                     UintegerValue(256 * 1024));  // 256KB

  // 设置接收缓冲区大小（单位字节）
  Config::SetDefault("ns3::TcpSocket::RcvBufSize",
                     UintegerValue(6291456 * 3));  // 6 MB

  // 设置 TCP 初始拥塞窗口（单位 MSS 包数）
  // Config::SetDefault("ns3::TcpSocket::InitialCwnd",
  //                    UintegerValue(10));  // 10 个报文段

  // 设置延迟确认计数 2个包
  Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(delAckCount));

  // 设置每个 TCP 段的大小（MSS，单位字节）
  Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1448));

  // 设置网卡队列最大长度，这里是 1 个包（"1p"）
  Config::SetDefault("ns3::DropTailQueue<Packet>::MaxSize",
                     QueueSizeValue(QueueSize("1p")));

  // 开启ECN标志
  // Config::SetDefault("ns3::TcpSocketBase::UseEcn", StringValue("On"));
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
  // node 0 是视频节点
  // node 1 是音频节点
  videoId = servers.Get(0)->GetId();
  audioId = servers.Get(1)->GetId();
  clientId = clients.Get(0)->GetId();

  /* -------------------------------------------------------------------------
  //                               链路配置
    2个服务器，服务器1存储视频，服务器2存储音频
    3/4个路由
    1个客户端
   -------------------------------------------------------------------------
  */
  std::string delay = onelinedelay(m_rtt) + "ms";  // 单向时延
  std::string bandwidth_v1 = std::to_string(m_bd_v1) + "Mbps";
  std::string n_pkt = bufferpkt(m_rtt, m_bd_v1,
                                m_buffersize_time);  // Queue 大小
  n_pkt = "100";
  NS_LOG_UNCOND("Buffer size (in packets): " << n_pkt);
  // 设置路由器拥塞控制的队列长度
  Config::SetDefault(queueDisc + "::MaxSize",
                     QueueSizeValue(QueueSize(n_pkt + "p")));

  PointToPointHelper p2p_v1;
  p2p_v1.SetDeviceAttribute("DataRate", StringValue(bandwidth_v1));
  p2p_v1.SetChannelAttribute("Delay", StringValue(delay));

  // 有线链路 server0-r0，server1-r1，r0-r2，r1-r2
  NetDeviceContainer video_to_r0, audio_to_r1, r1_to_r2, r0_to_r2, r2_to_c;
  video_to_r0 = p2p_v1.Install(servers.Get(0), routers.Get(0));
  audio_to_r1 = p2p_v1.Install(servers.Get(1), routers.Get(1));
  r1_to_r2 = p2p_v1.Install(routers.Get(1), routers.Get(2));
  r0_to_r2 = p2p_v1.Install(routers.Get(0), routers.Get(2));
  r2_to_c = p2p_v1.Install(routers.Get(2), clients.Get(0));

  // -------------------------------------------------------------------------
  //                               安装 TCP/IP 协议栈
  // -------------------------------------------------------------------------

  InternetStackHelper stack;
  stack.InstallAll();

  // -------------------------------------------------------------------------
  //                          设置瓶颈队列
  // -------------------------------------------------------------------------

  TrafficControlHelper tch1;
  // tch1.SetRootQueueDisc(queueDisc);
  // tch1.SetRootQueueDisc("ns3::CoDelQueueDisc", "UseEcn", BooleanValue(true));
  // tch1.SetRootQueueDisc("ns3::CoDelQueueDisc");
  // tch1.SetRootQueueDisc("ns3::FqCoDelQueueDisc");
  tch1.SetRootQueueDisc("ns3::FqCoDelQueueDisc", "UseEcn", BooleanValue(true));
  QueueDiscContainer qdiscs = tch1.Install(r2_to_c);
  Ptr<QueueDisc> bottleneckQ = qdiscs.Get(0);

  bottleneckQ->TraceConnectWithoutContext("Enqueue", MakeCallback(&OnEnqueue));
  bottleneckQ->TraceConnectWithoutContext("Dequeue", MakeCallback(&OnDequeue));
  bottleneckQ->TraceConnectWithoutContext("Drop", MakeCallback(&OnDrop));

  Ptr<NetDevice> nd = r2_to_c.Get(0);
  Ptr<PointToPointNetDevice> p2pDev = DynamicCast<PointToPointNetDevice>(nd);
  // 获取发送队列
  Ptr<Queue<Packet>> txQueue = p2pDev->GetQueue();

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

  videoAddress = interfaces1.GetAddress(0);  // 10.1.1.1
  audioAddress = interfaces2.GetAddress(0);  // 10.1.2.1
  NS_LOG_UNCOND("Server1 IP: " << videoAddress
                               << " , Server2 IP: " << audioAddress);

  // 生成其他路由（边缘节点到骨干等）
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  // -------------------------------------------------------------------------
  //                                创建应用
  // -------------------------------------------------------------------------

  // uint16_t videoport = 10000;
  // uint16_t audioport = 10001;
  uint16_t videoport = 10;
  uint16_t audioport = 11;
  // ------------------- VideoServer --------------------
  MultiTcpAvStreamServerHelper videoserverHelper(videoport, audioport);
  ApplicationContainer videoserverApp =
      videoserverHelper.Install(servers.Get(0));
  videoserverApp.Start(Seconds(1.0));

  // ------------------- AudioServer --------------------
  MultiTcpAvStreamServerHelper audioserverHelper(videoport, audioport);
  ApplicationContainer audioserverApp =
      audioserverHelper.Install(servers.Get(1));
  audioserverApp.Start(Seconds(1.0));

  // -------------------------------------------------------------------------
  // 客户端
  // -------------------------------------------------------------------------

  // 创建一个 vector，用来存放客户端节点和对应自适应算法名称
  // std::pair<Ptr<Node>, std::string>
  // ：第一个元素是节点对象，第二个元素是算法名称
  std::vector<std::pair<Ptr<Node>, std::pair<std::string, std::string>>> client;

  // 获取 clients 容器的第一个节点（clients 是之前创建的客户端节点集合）
  NodeContainer::Iterator i = clients.Begin();

  // 将第一个客户端节点和自适应算法名称放入 client 容器
  client.push_back(std::make_pair(
      *i, std::make_pair(video_adaptationAlgo, audio_adaptationAlgo)));
  // *i 表示节点对象，adaptationAlgo 是ABR算法名称（如 "festive"）

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

  // 开启共享瓶颈感知的ABR算法
  // clientHelperVideo.SetAttribute("EnableSharedBottleneckAwareAbr",
  //                                BooleanValue(true));

  // 开启 MPTCP-SBD（INFOCOM 2016） 算法支持
  clientHelperVideo.SetAttribute("EnableMptcpSbd", BooleanValue(true));

  // 安装客户端应用到节点上
  // Install() 会根据 client
  // 容器中的节点和算法创建对应应用，并返回ApplicationContainer
  ApplicationContainer clientApps1 = clientHelperVideo.Install(client);

  // 获取客户端应用
  g_clientApp = DynamicCast<MultiTcpAvStreamClient>(clientApps1.Get(0));
  NS_ASSERT(g_clientApp);

  // 为每个客户端应用设置启动时间
  for (uint i = 0; i < clientApps1.GetN(); i++) {
    // 计算启动时间，避免所有客户端同时启动，造成瞬时拥塞
    double startTime = 2.0 + ((i * 3) / 100.0);            // i*0.03 秒的延迟
    clientApps1.Get(i)->SetStartTime(Seconds(startTime));  // 设置应用启动时间
  }

  // 输出网络拓扑路由结构
  std::string routetablefile = basePrefix + "/routetable.txt";
  std::ofstream ofs(routetablefile);
  Ptr<OutputStreamWrapper> fileStream = Create<OutputStreamWrapper>(&ofs);
  Ipv4GlobalRoutingHelper::PrintRoutingTableAllAt(Seconds(0.2), fileStream);

  // -------------------------------------------------------------------------
  //                         Trace追踪
  //  1.在瓶颈节点处统计音频、视频2个流的带宽占用比例
  //  2.开启ECN标志，在路由器包头打上CE标志，在客户端检测共享瓶颈
  // -------------------------------------------------------------------------

  // 统计2个流在瓶颈链路处的链路带宽资源的占比
  Ptr<PointToPointNetDevice> bottleneckDev =
      r2_to_c.Get(0)->GetObject<PointToPointNetDevice>();

  // r2 节点
  Ptr<Node> r2 = routers.Get(2);

  // IPv4 协议栈
  Ptr<Ipv4> ipv4_r2 = r2->GetObject<Ipv4>();

  // 瓶颈接口 index
  g_bottleneckIf = ipv4_r2->GetInterfaceForDevice(r2_to_c.Get(0));

  // 连接 Tx trace
  ipv4_r2->TraceConnectWithoutContext("Tx",
                                      MakeCallback(&BottleneckIpv4TxTrace));

  Simulator::Schedule(Seconds(1.0), &ReportBandwidthShare);

  // 6. **设置客户端 Trace,在客户端检测CE标志**
  Ptr<Node> client_0 = clients.Get(0);
  Ptr<Ipv4> ipv4_cl0 = client_0->GetObject<Ipv4>();
  ipv4_cl0->TraceConnectWithoutContext("Rx", MakeCallback(&ClientIpv4RxTrace));

  // -------------------------------------------------------------------------
  //                         仿真运行
  // -------------------------------------------------------------------------

  // 每秒读取带宽trace，模拟时变的瓶颈链路
  LoadBandwidthTrace("./trace/4G/train_003(v4_1).txt");
  // LoadBandwidthTrace("./trace/test.txt");
  Simulator::Schedule(Seconds(0.0), &UpdateLinkBandwidth, bottleneckDev, 0);

  // 打印瓶颈处数据包情况
  Simulator::Schedule(Seconds(2.0), &PrintQueueSize, bottleneckQ);

  Simulator::Schedule(Seconds(2.0), &PeriodicQueueStat);

  FlowMonitorHelper fmHelper;  // 安装流监控器
  Ptr<FlowMonitor> fm = fmHelper.InstallAll();

  // Simulator::Schedule(Seconds(2.1), []() {
  //   // BytesInFlight
  //   Config::Connect(
  //       "/NodeList/0/$ns3::TcpL4Protocol/SocketList/*/BytesInFlight",
  //       MakeCallback(&VideoBytesInFlightTrace));
  //   Config::Connect(
  //       "/NodeList/1/$ns3::TcpL4Protocol/SocketList/*/BytesInFlight",
  //       MakeCallback(&AudioBytesInFlightTrace));

  //   // Cwnd
  //   Config::Connect(
  //       "/NodeList/0/$ns3::TcpL4Protocol/SocketList/*/CongestionWindow",
  //       MakeCallback(&VideoCwndTrace));
  //   Config::Connect(
  //       "/NodeList/1/$ns3::TcpL4Protocol/SocketList/*/CongestionWindow",
  //       MakeCallback(&AudioCwndTrace));
  //   // Rtt
  //   Config::Connect("/NodeList/0/$ns3::TcpL4Protocol/SocketList/*/RTT",
  //                   MakeCallback(&VideoRTTTrace));
  //   Config::Connect("/NodeList/1/$ns3::TcpL4Protocol/SocketList/*/RTT",
  //                   MakeCallback(&AudioRTTTrace));
  // });

  Simulator::Stop(Seconds(150));
  // Simulator::Stop(Seconds(110));
  // Simulator::Schedule(Seconds(0.1), &SetCCA, servers.Get(0), "TcpBbr");
  Simulator::Run();
  Simulator::Destroy();

  // 统计函数
  ComputePacketLossRate(fm);

  if (g_bwShareFile.is_open()) {
    g_bwShareFile.close();
  }
  return 0;
}