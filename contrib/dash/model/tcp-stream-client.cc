#include "tcp-stream-client.h"

#include <errno.h>
#include <math.h>
#include <ns3/core-module.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstring>
#include <ctime>
#include <iomanip>
#include <iterator>
#include <numeric>
#include <sstream>
#include <stdexcept>

#include "ns3/global-value.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv6-address.h"
#include "ns3/log.h"
#include "ns3/nstime.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/socket.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/uinteger.h"
#include "tcp-stream-server.h"

namespace ns3 {

template <typename T>
std::string ToString(T val) {
  std::stringstream stream;
  stream << val;
  return stream.str();
}

NS_LOG_COMPONENT_DEFINE("TcpStreamClientApplication");

NS_OBJECT_ENSURE_REGISTERED(TcpStreamClient);

void TcpStreamClient::Controller(controllerEvent event) {
  NS_LOG_FUNCTION(this);  // NS3 日志宏，打印函数调用信息

  // 如果当前状态是初始状态
  if (state == initial) {
    RequestRepIndex();    // 调用自适应算法，获取当前段应该下载的码率索引
    state = downloading;  // 状态切换为 downloading，表示开始下载
    // 发送当前段请求给服务器，m_videoData.segmentSize 是段大小矩阵
    Send(m_videoData.segmentSize.at(m_currentRepIndex).at(m_segmentCounter));
    return;  // 结束本次 Controller 调用
  }

  // 如果当前状态是 downloading（只下载，不播放）
  if (state == downloading) {
    PlaybackHandle();  // 尝试播放缓冲区中的 segment
    if (m_currentPlaybackIndex <=
        m_lastSegmentIndex) {      // 如果还有 segment 未播放
      /*  e_d  */                  // 状态注释标记，表示下载事件
      m_segmentCounter++;          // 下载计数器 +1，准备下载下一段
      RequestRepIndex();           // 获取下一段应该下载的码率索引
      state = downloadingPlaying;  // 状态切换为下载+播放
      // 发送下一段下载请求
      Send(m_videoData.segmentSize.at(m_currentRepIndex).at(m_segmentCounter));
    } else {            // 如果已经播放到最后一个 segment
      /*  e_df  */      // 状态注释标记，表示下载完成事件
      state = playing;  // 状态切换为播放状态
    }
    controllerEvent ev = playbackFinished;  // 定义一个事件类型，表示播放完成
    // 调度下一次 Controller 调用，在 segment 时长后触发播放完成事件
    Simulator::Schedule(MicroSeconds(m_videoData.segmentDuration),
                        &TcpStreamClient::Controller, this, ev);
    return;  // 结束本次 Controller 调用
  }

  // 如果当前状态是 downloadingPlaying（下载+播放）
  else if (state == downloadingPlaying) {
    if (event == downloadFinished) {                // 如果触发事件是下载完成
      if (m_segmentCounter < m_lastSegmentIndex) {  // 如果还有 segment 待下载
        m_segmentCounter++;                         // 下载计数器 +1
        RequestRepIndex();                          // 获取下一段码率索引
      }

      if (m_bDelay > 0 && m_segmentCounter <= m_lastSegmentIndex) {
        /*  e_dirs */  // 延迟下载事件标记
        NS_LOG_FUNCTION(this << "延迟下载事件发生"
                             << m_bDelay);  // 记录函数调用和事件
        state = playing;                    // 切换到播放状态
        controllerEvent ev = irdFinished;   // 设置事件为延迟下载完成
        // 调度延迟事件触发
        Simulator::Schedule(MicroSeconds(m_bDelay),
                            &TcpStreamClient::Controller, this, ev);
      } else if (m_segmentCounter ==
                 m_lastSegmentIndex) {  // 如果当前下载最后一段
        /*  e_df */                     // 下载完成标记
        state = playing;                // 切换为播放状态
      } else {                          // 如果还有 segment 待下载
        /*  e_d */                      // 下载事件标记
        // 发送下一段下载请求
        Send(
            m_videoData.segmentSize.at(m_currentRepIndex).at(m_segmentCounter));
      }
    } else if (event == playbackFinished) {  // 如果触发事件是播放完成
      if (!PlaybackHandle()) {  // 尝试播放下一段，如果返回 false 表示缓冲中还有
                                // segment
        /*  e_pb */             // 播放缓冲标记
        controllerEvent ev = playbackFinished;  // 设置播放完成事件
        // 调度下一次播放完成事件
        Simulator::Schedule(MicroSeconds(m_videoData.segmentDuration),
                            &TcpStreamClient::Controller, this, ev);
      } else {                // 缓冲为空，无法播放
        /*  e_pu */           // 播放空缓冲标记
        state = downloading;  // 切换回只下载状态
      }
    }
    return;  // 结束本次 Controller 调用
  }

  // 如果当前状态是 playing（只播放）
  else if (state == playing) {
    if (event == irdFinished) {    // 如果延迟下载事件完成
      /*  e_irc */                 // 延迟下载完成标记
      state = downloadingPlaying;  // 状态切换回下载+播放
      // 发送当前段下载请求
      Send(m_videoData.segmentSize.at(m_currentRepIndex).at(m_segmentCounter));
    } else if (event == playbackFinished &&
               m_currentPlaybackIndex <
                   m_lastSegmentIndex) {      // 如果播放完成，且还有 segment
      /*  e_pb */                             // 播放完成标记
      PlaybackHandle();                       // 播放缓冲区中的 segment
      controllerEvent ev = playbackFinished;  // 生成播放完成事件
      // 调度下一次播放完成事件
      Simulator::Schedule(MicroSeconds(m_videoData.segmentDuration),
                          &TcpStreamClient::Controller, this, ev);
    } else if (event == playbackFinished &&
               m_currentPlaybackIndex ==
                   m_lastSegmentIndex) {  // 如果播放完成，且已经是最后 segment
      PlaybackHandle();                   // 播放最后一段
      /*  e_pf */                         // 播放完成标记
      state = terminal;                   // 状态切换为终止
      StopApplication();                  // 停止客户端应用
    }
    return;  // 结束本次 Controller 调用
  }
}

// 定义 NS3 类型系统中的 TypeId，用于注册类信息和属性
TypeId TcpStreamClient::GetTypeId(void) {
  static TypeId tid =
      TypeId("ns3::TcpStreamClient")          // 类的名字，用于 NS3 类型系统
          .SetParent<Application>()           // 指定父类为 ns3::Application
          .SetGroupName("Applications")       // 所属模块组
          .AddConstructor<TcpStreamClient>()  // 添加默认构造函数

          // 添加 RemoteAddress 属性，表示客户端要连接的远端 IP 地址
          .AddAttribute("RemoteAddress",
                        "The destination Address of the outbound packets",
                        AddressValue(),  // 默认值为空
                        MakeAddressAccessor(
                            &TcpStreamClient::m_peerAddress),  // 绑定到类成员
                        MakeAddressChecker())                  // 类型检查

          // 添加 RemotePort 属性，表示客户端要连接的远端端口
          .AddAttribute("RemotePort",
                        "The destination port of the outbound packets",
                        UintegerValue(0),  // 默认值 0
                        MakeUintegerAccessor(&TcpStreamClient::m_peerPort),
                        MakeUintegerChecker<uint16_t>())

          // 添加 SegmentDuration 属性，表示每个视频段的持续时间（微秒）
          .AddAttribute(
              "SegmentDuration", "The duration of a segment in microseconds",
              UintegerValue(2000000),  // 默认 2 秒
              MakeUintegerAccessor(&TcpStreamClient::m_segmentDuration),
              MakeUintegerChecker<uint64_t>())

          /*
              修改这一段代码：SegmentSizeFilePath 属性，将其拆分为
              VideoSegmentSizeFilePath 与 AudioSegmentSizeFilePath

              原属性及其 m_segmentSizeFilePath 保留处理
          */

          // 添加 SegmentSizeFilePath 属性，指定段大小文件路径
          .AddAttribute(
              "SegmentSizeFilePath",
              "The relative path (from ns-3.x directory) to the file "
              "containing the segment sizes in bytes",
              StringValue("bitrates.txt"),  // 默认文件名
              MakeStringAccessor(&TcpStreamClient::m_segmentSizeFilePath),
              MakeStringChecker())

          // 添加 VideoSegmentSizeFilePath 属性，指定视频段大小文件路径
          .AddAttribute(
              "VideoSegmentSizeFilePath",
              "The relative path (from ns-3.x directory) to the video file "
              "containing the segment sizes in bytes",
              StringValue("bitrates.txt"),  // 默认文件名
              MakeStringAccessor(&TcpStreamClient::m_videosegmentSizeFilePath),
              MakeStringChecker())

          // 添加 AudioSegmentSizeFilePath 属性，指定视频段大小文件路径
          .AddAttribute(
              "AudioSegmentSizeFilePath",
              "The relative path (from ns-3.x directory) to the audio file "
              "containing the segment sizes in bytes",
              StringValue("bitrates.txt"),  // 默认文件名
              MakeStringAccessor(&TcpStreamClient::m_audiosegmentSizeFilePath),
              MakeStringChecker())

          // 添加 SimulationId 属性，用于日志标识当前仿真
          .AddAttribute(
              "SimulationId",
              "The ID of the current simulation, for logging purposes",
              UintegerValue(0),
              MakeUintegerAccessor(&TcpStreamClient::m_simulationId),
              MakeUintegerChecker<uint32_t>())

          // 添加 NumberOfClients 属性，总客户端数，用于日志
          .AddAttribute(
              "NumberOfClients",
              "The total number of clients for this simulation, for logging "
              "purposes",
              UintegerValue(1),
              MakeUintegerAccessor(&TcpStreamClient::m_numberOfClients),
              MakeUintegerChecker<uint16_t>())

          // 添加 ClientId 属性，用于标识单个客户端
          .AddAttribute(
              "ClientId",
              "The ID of the this client object, for logging purposes",
              UintegerValue(0),
              MakeUintegerAccessor(&TcpStreamClient::m_clientId),
              MakeUintegerChecker<uint32_t>());
  return tid;  // 返回 TypeId
}

// TcpStreamClient 的默认构造函数
TcpStreamClient::TcpStreamClient() {
  NS_LOG_FUNCTION(this);  // NS3 日志宏，记录构造函数调用
  m_socket = 0;           // Socket 初始化为空
  m_data = 0;             // 数据缓存初始化为空
  m_dataSize = 0;         // 数据缓存大小初始化为 0
  state = initial;        // 初始状态

  m_currentRepIndex = 0;       // 当前码率索引初始化为 0
  m_segmentCounter = 0;        // 已下载段计数初始化为 0
  m_bDelay = 0;                // 下载延迟初始化为 0
  m_bytesReceived = 0;         // 已接收字节初始化为 0
  m_segmentsInBuffer = 0;      // 缓冲区段数初始化为 0
  m_bufferUnderrun = false;    // 缓冲区空状态初始化为 false
  m_currentPlaybackIndex = 0;  // 当前播放索引初始化为 0
}

// 初始化客户端，选择自适应算法并加载段大小
void TcpStreamClient::Initialise(std::string algorithm, uint16_t clientId) {
  NS_LOG_FUNCTION(this);  // 日志记录

  m_videoData.segmentDuration = m_segmentDuration;  // 设置视频段持续时间

  // 从文件读取段大小，如果失败则终止仿真
  if (ReadInBitrateValues(ToString(m_segmentSizeFilePath)) == -1) {
    NS_LOG_ERROR("Opening test bitrate file failed. Terminating.\n");
    Simulator::Stop();     // 停止仿真
    Simulator::Destroy();  // 清理资源
  }

  // 设置最后一个段索引
  m_lastSegmentIndex = (int64_t)m_videoData.segmentSize.at(0).size() - 1;
  // 设置最大码率索引
  m_highestRepIndex = m_videoData.averageBitrate.size() - 1;

  // 根据算法名称选择不同的自适应算法
  if (algorithm == "tobasco") {
    algo = new TobascoAlgorithm(m_videoData, m_playbackData, m_bufferData,
                                m_throughput);
  } else if (algorithm == "panda") {
    algo = new PandaAlgorithm(m_videoData, m_playbackData, m_bufferData,
                              m_throughput);
  } else if (algorithm == "festive") {
    algo = new FestiveAlgorithm(m_videoData, m_playbackData, m_bufferData,
                                m_throughput);
  } else {  // 非法算法名称
    NS_LOG_ERROR("Invalid algorithm name entered. Terminating.");
    StopApplication();     // 停止应用
    Simulator::Stop();     // 停止仿真
    Simulator::Destroy();  // 清理资源
  }

  m_algoName = algorithm;  // 保存算法名称

  // 初始化各种日志文件
  InitializeLogFiles(ToString(m_simulationId), ToString(m_clientId),
                     ToString(m_numberOfClients));
}

// TcpStreamClient 析构函数，释放资源
TcpStreamClient::~TcpStreamClient() {
  NS_LOG_FUNCTION(this);  // NS3 日志记录，输出函数调用
  m_socket = 0;           // 断开 Socket 引用

  delete algo;      // 释放自适应算法对象
  algo = NULL;      // 避免悬空指针
  delete[] m_data;  // 释放数据缓存数组
  m_data = 0;       // 设置为空指针
  m_dataSize = 0;   // 重置数据大小
}

// 请求下一个视频段的码率索引
void TcpStreamClient::RequestRepIndex() {
  NS_LOG_FUNCTION(this);
  algorithmReply answer;  // 存储自适应算法的回复

  // 调用自适应算法获取下一段应该使用的码率
  answer = algo->GetNextRep(m_segmentCounter, m_clientId);
  m_currentRepIndex = answer.nextRepIndex;  // 更新当前码率索引

  // 确保算法返回的码率索引不超过最高码率
  NS_ASSERT_MSG(answer.nextRepIndex <= m_highestRepIndex,
                "The algorithm returned a representation index that's higher "
                "than the maximum");

  // 保存播放序列中的码率索引，用于后续的日志记录
  m_playbackData.playbackIndex.push_back(answer.nextRepIndex);

  m_bDelay = answer.nextDownloadDelay;  // 更新下载延迟
  LogAdaptation(answer);                // 记录适应性决策到日志
}

// 发送数据包模板函数
template <typename T>
void TcpStreamClient::Send(T& message) {
  NS_LOG_FUNCTION(this);
  PreparePacket(message);  // 将消息序列化成字节数组
  Ptr<Packet> p;
  p = Create<Packet>(m_data, m_dataSize);  // 创建 NS3 数据包对象
  m_downloadRequestSent = Simulator::Now().GetMicroSeconds();  // 记录发送时间
  m_socket->Send(p);  // 通过 Socket 发送数据包
}

// 处理从服务器接收到的数据
void TcpStreamClient::HandleRead(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);  // NS3 日志宏，记录函数调用及 socket 对象
  Ptr<Packet> packet;               // 定义接收的数据包指针

  // 如果当前段还未接收任何数据，则记录接收起始时间
  if (m_bytesReceived == 0) {
    m_transmissionStartReceivingSegment = Simulator::Now().GetMicroSeconds();
  }

  uint32_t packetSize;  // 保存每个接收到的数据包大小

  // 循环接收 Socket 中的所有可用数据包
  while ((packet = socket->Recv())) {  // socket->Recv() 返回
                                       // Ptr<Packet>，若无数据返回 nullptr
    packetSize = packet->GetSize();    // 获取当前数据包大小（字节数）
    LogThroughput(packetSize);         // 记录吞吐量日志
    m_bytesReceived += packetSize;     // 累加当前段已接收的字节数

    // 如果当前段已经接收完整，则调用处理函数
    if (m_bytesReceived ==
        m_videoData.segmentSize.at(m_currentRepIndex).at(m_segmentCounter)) {
      SegmentReceivedHandle();  // 更新缓冲区、吞吐量、日志等
    }
  }
}

// 从文件读取视频段大小和平均码率
int TcpStreamClient::ReadInBitrateValues(std::string segmentSizeFile) {
  NS_LOG_FUNCTION(this);                 // NS3 日志宏，记录函数调用
  std::ifstream myfile;                  // 文件输入流
  myfile.open(segmentSizeFile.c_str());  // 打开指定的段大小文件
  if (!myfile) {                         // 文件打开失败
    return -1;                           // 返回错误码
  }

  std::string temp;                 // 临时保存每行文本
  int64_t averageByteSizeTemp = 0;  // 临时保存每行的平均字节数

  // 按行读取文件
  while (std::getline(myfile, temp)) {
    if (temp.empty()) {  // 遇到空行停止读取
      break;
    }

    std::istringstream buffer(temp);  // 将一行转换为字符串流
    // 将一行数据解析为 int64_t 类型的向量，表示该段各码率下的大小
    std::vector<int64_t> line((std::istream_iterator<int64_t>(buffer)),
                              std::istream_iterator<int64_t>());
    m_videoData.segmentSize.push_back(line);  // 保存到 segmentSize 中

    // 计算该行的平均字节数
    averageByteSizeTemp =
        (int64_t)std::accumulate(line.begin(), line.end(), 0.0) / line.size();

    // 将平均字节数转化为比特率（bit/s），并保存到 averageBitrate
    m_videoData.averageBitrate.push_back(
        (8.0 * averageByteSizeTemp) /                // 字节转比特
        (m_videoData.segmentDuration / 1000000.0));  // 除以段时长（秒）
  }

  // 确保成功读取到段大小信息
  NS_ASSERT_MSG(!m_videoData.segmentSize.empty(),
                "No segment sizes read from file.");

  return 1;  // 成功返回 1
}

void TcpStreamClient::SegmentReceivedHandle() {
  NS_LOG_FUNCTION(this);  // NS-3 日志宏，记录函数调用，便于调试

  // 记录当前时间，表示当前段接收完成的时间
  m_transmissionEndReceivingSegment = Simulator::Now().GetMicroSeconds();

  // 将接收完成时间存入缓冲时间记录数组
  m_bufferData.timeNow.push_back(m_transmissionEndReceivingSegment);

  if (m_segmentCounter > 0) {
    // 如果不是第一段视频，计算缓冲的“旧缓冲量”
    // old buffer level = 上一次缓冲量 - 自上次接收完成以来播放消耗的时间
    // 如果结果为负数，则取0，保证缓冲量不会为负
    m_bufferData.bufferLevelOld.push_back(
        std::max(m_bufferData.bufferLevelNew.back() -
                     (m_transmissionEndReceivingSegment -
                      m_throughput.transmissionEnd.back()),
                 (int64_t)0));
  } else {
    // 如果是第一段，旧缓冲量为0
    m_bufferData.bufferLevelOld.push_back(0);
  }

  // 新缓冲量 = 旧缓冲量 + 当前段的时长
  m_bufferData.bufferLevelNew.push_back(m_bufferData.bufferLevelOld.back() +
                                        m_videoData.segmentDuration);

  // 记录吞吐量相关信息
  m_throughput.bytesReceived.push_back(
      m_videoData.segmentSize.at(m_currentRepIndex)
          .at(m_segmentCounter));  // 本段大小
  m_throughput.transmissionStart.push_back(
      m_transmissionStartReceivingSegment);  // 接收开始时间
  m_throughput.transmissionRequested.push_back(
      m_downloadRequestSent);  // 请求下载时间
  m_throughput.transmissionEnd.push_back(
      m_transmissionEndReceivingSegment);  // 接收结束时间

  // 写入下载日志
  LogDownload();

  // 写入缓冲日志
  LogBuffer();

  // 缓冲中段数 +1
  m_segmentsInBuffer++;

  // 重置已接收字节数，准备接收下一段
  m_bytesReceived = 0;

  // 如果当前段是最后一段，取消延迟
  if (m_segmentCounter == m_lastSegmentIndex) {
    m_bDelay = 0;
  }

  // 通知 Controller 事件：当前段下载完成
  controllerEvent event = downloadFinished;
  Controller(event);
}

bool TcpStreamClient::PlaybackHandle() {
  NS_LOG_FUNCTION(this);  // 日志宏

  // 当前模拟时间（微秒）
  int64_t timeNow = Simulator::Now().GetMicroSeconds();

  // 如果缓冲区为空且还有剩余段未播放，说明发生缓冲不足（buffer underrun）
  if (m_segmentsInBuffer == 0 && m_currentPlaybackIndex < m_lastSegmentIndex &&
      !m_bufferUnderrun) {
    m_bufferUnderrun = true;  // 标记缓冲不足
    // 写入缓冲不足日志：记录开始时间
    bufferUnderrunLog << std::setfill(' ') << std::setw(26)
                      << timeNow / (double)1000000 << " ";
    bufferUnderrunLog.flush();  // 立即刷新到文件
    return true;                // 返回 true 表示缓冲不足
  }
  // 如果缓冲区中有数据
  else if (m_segmentsInBuffer > 0) {
    if (m_bufferUnderrun) {  // 如果之前缓冲不足，标记已恢复
      m_bufferUnderrun = false;
      bufferUnderrunLog << std::setfill(' ') << std::setw(13)
                        << timeNow / (double)1000000
                        << "\n";  // 记录缓冲恢复时间
      bufferUnderrunLog.flush();
    }
    // 将当前播放段开始时间存入播放日志
    m_playbackData.playbackStart.push_back(timeNow);
    LogPlayback();             // 写入播放日志
    m_segmentsInBuffer--;      // 缓冲区中段数减少
    m_currentPlaybackIndex++;  // 当前播放段索引加1
    return false;              // 返回 false 表示播放成功
  }

  return true;  // 返回 true 表示没有播放数据
}

// 设置远端地址和端口（通用 Address 类型）
void TcpStreamClient::SetRemote(Address ip, uint16_t port) {
  NS_LOG_FUNCTION(this << ip << port);  // 日志记录函数调用
  m_peerAddress = ip;                   // 保存远端 IP 地址
  m_peerPort = port;                    // 保存远端端口号
}

// 设置远端 IPv4 地址和端口
void TcpStreamClient::SetRemote(Ipv4Address ip, uint16_t port) {
  NS_LOG_FUNCTION(this << ip << port);
  m_peerAddress = Address(ip);  // 将 IPv4 地址转换为通用 Address 类型
  m_peerPort = port;
}

// 设置远端 IPv6 地址和端口
void TcpStreamClient::SetRemote(Ipv6Address ip, uint16_t port) {
  NS_LOG_FUNCTION(this << ip << port);
  m_peerAddress = Address(ip);  // 将 IPv6 地址转换为通用 Address 类型
  m_peerPort = port;
}

// 对象释放时的清理操作
void TcpStreamClient::DoDispose(void) {
  NS_LOG_FUNCTION(this);
  Application::DoDispose();  // 调用父类的清理方法
}

// 启动应用程序
void TcpStreamClient::StartApplication(void) {
  NS_LOG_FUNCTION(this);

  // 如果 socket 尚未创建
  if (m_socket == 0) {
    // 创建 TCP socket
    TypeId tid = TypeId::LookupByName("ns3::TcpSocketFactory");
    m_socket = Socket::CreateSocket(GetNode(), tid);

    // 根据地址类型连接远端
    if (Ipv4Address::IsMatchingType(m_peerAddress) == true) {
      m_socket->Connect(InetSocketAddress(
          Ipv4Address::ConvertFrom(m_peerAddress), m_peerPort));
    } else if (Ipv6Address::IsMatchingType(m_peerAddress) == true) {
      m_socket->Connect(Inet6SocketAddress(
          Ipv6Address::ConvertFrom(m_peerAddress), m_peerPort));
    }

    // 设置连接成功/失败回调
    m_socket->SetConnectCallback(
        MakeCallback(&TcpStreamClient::ConnectionSucceeded, this),
        MakeCallback(&TcpStreamClient::ConnectionFailed, this));

    // 设置接收数据回调
    m_socket->SetRecvCallback(MakeCallback(&TcpStreamClient::HandleRead, this));
  }
}

// 停止应用程序
void TcpStreamClient::StopApplication() {
  NS_LOG_FUNCTION(this);

  if (m_socket != 0) {
    m_socket->Close();  // 关闭 socket
    m_socket->SetRecvCallback(
        MakeNullCallback<void, Ptr<Socket>>());  // 清空回调
    m_socket = 0;                                // 重置 socket
  }

  // 关闭所有日志文件
  downloadLog.close();
  playbackLog.close();
  adaptationLog.close();
  bufferLog.close();
  throughputLog.close();
  bufferUnderrunLog.close();
}

// 将消息转换为可发送的数据包
template <typename T>
void TcpStreamClient::PreparePacket(T& message) {
  NS_LOG_FUNCTION(this << message);

  std::ostringstream ss;
  ss << message;                            // 将消息序列化为字符串
  ss.str();                                 // 获取字符串
  uint32_t dataSize = ss.str().size() + 1;  // 字节数 (+1表示末尾 '\0')

  // 如果数据大小发生变化，重新分配缓冲区
  if (dataSize != m_dataSize) {
    delete[] m_data;
    m_data = new uint8_t[dataSize];
    m_dataSize = dataSize;
  }

  // 拷贝消息到缓冲区
  memcpy(m_data, ss.str().c_str(), dataSize);
}

// 连接成功回调
void TcpStreamClient::ConnectionSucceeded(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);
  NS_LOG_LOGIC("Tcp Stream Client connection succeeded");

  controllerEvent event = init;  // 触发初始化事件
  Controller(event);             // 调用 Controller
}

// 连接失败回调
void TcpStreamClient::ConnectionFailed(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);
  NS_LOG_LOGIC("Tcp Stream Client connection failed");
}

// 记录单个包的吞吐量日志
void TcpStreamClient::LogThroughput(uint32_t packetSize) {
  NS_LOG_FUNCTION(this);
  throughputLog << std::setfill(' ') << std::setw(13)
                << Simulator::Now().GetMicroSeconds() / (double)1000000 << " "
                << std::setfill(' ') << std::setw(13) << packetSize << "\n";
  throughputLog.flush();  // 立即写入文件
}

// 记录下载事件日志
void TcpStreamClient::LogDownload() {
  NS_LOG_FUNCTION(this);
  downloadLog
      << std::setfill(' ') << std::setw(13) << m_segmentCounter << " "
      << std::setfill(' ') << std::setw(21)
      << m_downloadRequestSent / (double)1000000 << " " << std::setfill(' ')
      << std::setw(14) << m_transmissionStartReceivingSegment / (double)1000000
      << " " << std::setfill(' ') << std::setw(12)
      << m_transmissionEndReceivingSegment / (double)1000000 << " "
      << std::setfill(' ') << std::setw(12)
      << m_videoData.segmentSize.at(m_currentRepIndex).at(m_segmentCounter)
      << " " << std::setfill(' ') << std::setw(12) << "Y\n";
  downloadLog.flush();
}

// 记录缓冲状态日志
void TcpStreamClient::LogBuffer() {
  NS_LOG_FUNCTION(this);
  bufferLog << std::setfill(' ') << std::setw(13)
            << m_transmissionEndReceivingSegment / (double)1000000 << " "
            << std::setfill(' ') << std::setw(13)
            << m_bufferData.bufferLevelOld.back() / (double)1000000 << "\n"
            << std::setfill(' ') << std::setw(13)
            << m_transmissionEndReceivingSegment / (double)1000000 << " "
            << std::setfill(' ') << std::setw(13)
            << m_bufferData.bufferLevelNew.back() / (double)1000000 << "\n";
  bufferLog.flush();
}

// 记录自适应码率选择决策日志
void TcpStreamClient::LogAdaptation(algorithmReply answer) {
  NS_LOG_FUNCTION(this);
  adaptationLog << std::setfill(' ') << std::setw(13) << m_segmentCounter << " "
                << std::setfill(' ') << std::setw(9) << m_currentRepIndex << " "
                << std::setfill(' ') << std::setw(22)
                << answer.decisionTime / (double)1000000 << " "
                << std::setfill(' ') << std::setw(4) << answer.decisionCase
                << " " << std::setfill(' ') << std::setw(9)
                << answer.delayDecisionCase << "\n";
  adaptationLog.flush();
}

// 记录播放日志
void TcpStreamClient::LogPlayback() {
  NS_LOG_FUNCTION(this);
  playbackLog << std::setfill(' ') << std::setw(13) << m_currentPlaybackIndex
              << " " << std::setfill(' ') << std::setw(14)
              << Simulator::Now().GetMicroSeconds() / (double)1000000 << " "
              << std::setfill(' ') << std::setw(13)
              << m_playbackData.playbackIndex.at(m_currentPlaybackIndex)
              << "\n";
  playbackLog.flush();
}

// 初始化所有日志文件
void TcpStreamClient::InitializeLogFiles(std::string simulationId,
                                         std::string clientId,
                                         std::string numberOfClients) {
  NS_LOG_FUNCTION(this);

  // 构造下载日志文件路径并打开
  std::string dLog = dashLogDirectory + m_algoName + "/" + numberOfClients +
                     "/sim" + simulationId + "_" + "cl" + clientId + "_" +
                     "downloadLog.txt";
  downloadLog.open(dLog.c_str());
  downloadLog << "Segment_Index Download_Request_Sent Download_Start "
                 "Download_End Segment_Size Download_OK\n";
  downloadLog.flush();

  // 构造播放日志文件路径并打开
  std::string pLog = dashLogDirectory + m_algoName + "/" + numberOfClients +
                     "/sim" + simulationId + "_" + "cl" + clientId + "_" +
                     "playbackLog.txt";
  playbackLog.open(pLog.c_str());
  playbackLog << "Segment_Index Playback_Start Quality_Level\n";
  playbackLog.flush();

  // 构造自适应日志文件路径并打开
  std::string aLog = dashLogDirectory + m_algoName + "/" + numberOfClients +
                     "/sim" + simulationId + "_" + "cl" + clientId + "_" +
                     "adaptationLog.txt";
  adaptationLog.open(aLog.c_str());
  adaptationLog
      << "Segment_Index Rep_Level Decision_Point_Of_Time Case DelayCase\n";
  adaptationLog.flush();

  // 构造缓冲日志文件路径并打开
  std::string bLog = dashLogDirectory + m_algoName + "/" + numberOfClients +
                     "/sim" + simulationId + "_" + "cl" + clientId + "_" +
                     "bufferLog.txt";
  bufferLog.open(bLog.c_str());
  bufferLog << "     Time_Now  Buffer_Level \n";
  bufferLog.flush();

  // 构造吞吐量日志文件路径并打开
  std::string tLog = dashLogDirectory + m_algoName + "/" + numberOfClients +
                     "/sim" + simulationId + "_" + "cl" + clientId + "_" +
                     "throughputLog.txt";
  throughputLog.open(tLog.c_str());
  throughputLog << "     Time_Now Bytes Received \n";
  throughputLog.flush();

  // 构造缓冲不足日志文件路径并打开
  std::string buLog = dashLogDirectory + m_algoName + "/" + numberOfClients +
                      "/sim" + simulationId + "_" + "cl" + clientId + "_" +
                      "bufferUnderrunLog.txt";
  bufferUnderrunLog.open(buLog.c_str());
  bufferUnderrunLog << ("Buffer_Underrun_Started_At         Until \n");
  bufferUnderrunLog.flush();
}

}  // Namespace ns3
