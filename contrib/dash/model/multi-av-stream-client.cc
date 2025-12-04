// 包含多TCP流AV客户端的头文件
#include "multi-av-stream-client.h"

// 包含系统头文件
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// 包含C++标准库头文件
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iterator>
#include <numeric>
#include <sstream>
#include <stdexcept>

// 包含NS3核心模块
#include <ns3/core-module.h>

// 包含NS3网络相关头文件
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

// 声明使用ns3命名空间
namespace ns3 {

// 辅助函数：将任意类型转换为字符串
template <typename T>
std::string ToString(T val) {
  std::stringstream stream;  // 创建字符串流
  stream << val;             // 将值写入字符串流
  return stream.str();       // 返回字符串
}

// 定义日志组件名称
NS_LOG_COMPONENT_DEFINE("MultiTcpAvStreamClientApplication");

// 确保MultiTcpAvStreamClient类型在NS3对象系统中注册
NS_OBJECT_ENSURE_REGISTERED(MultiTcpAvStreamClient);

// 主控制器状态机函数
void MultiTcpAvStreamClient::Controller(controllerEvent event) {
  NS_LOG_FUNCTION(this << event);  // 记录函数调用和事件

  // 初始状态处理
  if (state == initial) {
    // 为两个流请求码率索引
    RequestRepIndex(VIDEO_STREAM);  // 请求视频流码率索引
    RequestRepIndex(AUDIO_STREAM);  // 请求音频流码率索引

    // 发送两个流的下载请求
    if (BothStreamsConnected()) {  // 确保两个流都已连接
      // 发送视频段请求
      Send(m_videoStream.m_video.segmentSize.at(m_videoStream.currentRepIndex)
               .at(m_videoStream.segmentCounter),
           VIDEO_STREAM);

      // 发送音频段请求
      Send(m_audioStream.m_video.segmentSize.at(m_audioStream.currentRepIndex)
               .at(m_audioStream.segmentCounter),
           AUDIO_STREAM);

      state = downloading;  // 切换到下载状态
    }
    return;
  }

  // 下载状态处理
  if (state == downloading) {
    PlaybackHandle();  // 尝试播放缓冲区中的段

    // 检查是否还有段需要播放
    if (m_currentPlaybackIndex <= m_videoStream.lastSegmentIndex) {
      // 两个流的下载计数器加1
      m_videoStream.segmentCounter++;
      m_audioStream.segmentCounter++;

      // 为两个流请求下一段的码率索引
      RequestRepIndex(VIDEO_STREAM);
      RequestRepIndex(AUDIO_STREAM);

      state = downloadingPlaying;  // 切换到下载+播放状态

      // 发送两个流的下一段请求
      Send(m_videoStream.m_video.segmentSize.at(m_videoStream.currentRepIndex)
               .at(m_videoStream.segmentCounter),
           VIDEO_STREAM);
      Send(m_audioStream.m_video.segmentSize.at(m_audioStream.currentRepIndex)
               .at(m_audioStream.segmentCounter),
           AUDIO_STREAM);
    } else {
      // 所有段都已播放，切换到播放状态
      state = playing;
    }

    // 调度下一次播放完成事件
    controllerEvent ev = playbackFinished;
    Simulator::Schedule(MicroSeconds(m_videoStream.segmentDuration),
                        &MultiTcpAvStreamClient::Controller, this, ev);
    return;
  }

  // 下载+播放状态处理
  else if (state == downloadingPlaying) {
    if (event == downloadFinished) {  // 下载完成事件
      // 检查两个流是否都完成了当前段的下载
      if (BothSegmentsReceived()) {
        // 如果还有段需要下载
        if (m_videoStream.segmentCounter < m_videoStream.lastSegmentIndex) {
          m_videoStream.segmentCounter++;  // 视频流下载计数器加1
          m_audioStream.segmentCounter++;  // 音频流下载计数器加1
          RequestRepIndex(VIDEO_STREAM);   // 请求视频流下一段码率
          RequestRepIndex(AUDIO_STREAM);   // 请求音频流下一段码率
        }

        // 检查是否需要延迟下载
        if (m_bDelay > 0 && m_videoStream.segmentCounter <= m_videoStream.lastSegmentIndex) {
          state = playing;                   // 切换到播放状态
          controllerEvent ev = irdFinished;  // 设置延迟完成事件
          // 调度延迟事件
          Simulator::Schedule(MicroSeconds(m_bDelay), &MultiTcpAvStreamClient::Controller, this,
                              ev);
        }
        // 如果是最后一段
        else if (m_videoStream.segmentCounter == m_videoStream.lastSegmentIndex) {
          state = playing;  // 切换到播放状态
        }
        // 还有段需要下载，继续下载
        else {
          Send(m_videoStream.m_video.segmentSize.at(m_videoStream.currentRepIndex)
                   .at(m_videoStream.segmentCounter),
               VIDEO_STREAM);
          Send(m_audioStream.m_video.segmentSize.at(m_audioStream.currentRepIndex)
                   .at(m_audioStream.segmentCounter),
               AUDIO_STREAM);
        }
      }
    } else if (event == playbackFinished) {  // 播放完成事件
      if (!PlaybackHandle()) {               // 尝试播放下一段
        // 调度下一次播放完成事件
        controllerEvent ev = playbackFinished;
        Simulator::Schedule(MicroSeconds(m_videoStream.segmentDuration),
                            &MultiTcpAvStreamClient::Controller, this, ev);
      } else {
        state = downloading;  // 缓冲为空，切回只下载状态
      }
    }
    return;
  }

  // 播放状态处理
  else if (state == playing) {
    if (event == irdFinished) {    // 延迟下载完成事件
      state = downloadingPlaying;  // 切回下载+播放状态
      // 发送两个流的下载请求
      Send(m_videoStream.m_video.segmentSize.at(m_videoStream.currentRepIndex)
               .at(m_videoStream.segmentCounter),
           VIDEO_STREAM);
      Send(m_audioStream.m_video.segmentSize.at(m_audioStream.currentRepIndex)
               .at(m_audioStream.segmentCounter),
           AUDIO_STREAM);
    } else if (event == playbackFinished &&
               m_currentPlaybackIndex < m_videoStream.lastSegmentIndex) {
      PlaybackHandle();  // 播放缓冲区中的段
      controllerEvent ev = playbackFinished;
      // 调度下一次播放完成事件
      Simulator::Schedule(MicroSeconds(m_videoStream.segmentDuration),
                          &MultiTcpAvStreamClient::Controller, this, ev);
    } else if (event == playbackFinished &&
               m_currentPlaybackIndex == m_videoStream.lastSegmentIndex) {
      PlaybackHandle();   // 播放最后一段
      state = terminal;   // 切换到终止状态
      StopApplication();  // 停止应用程序
    }
    return;
  }
}

// 获取类型标识函数
TypeId MultiTcpAvStreamClient::GetTypeId(void) {
  static TypeId tid =
      TypeId("ns3::MultiTcpAvStreamClient")          // 类型名称
          .SetParent<Application>()                  // 指定父类为Application
          .SetGroupName("Applications")              // 分组名称
          .AddConstructor<MultiTcpAvStreamClient>()  // 添加默认构造函数

          // 视频服务器地址属性
          .AddAttribute("VideoRemoteAddress", "The destination address of the video server",
                        AddressValue(),  // 默认值为空
                        MakeAddressAccessor(&MultiTcpAvStreamClient::GetVideoRemoteAddress,
                                            &MultiTcpAvStreamClient::SetVideoRemoteAddress),
                        MakeAddressChecker())

          // 视频服务器端口属性
          .AddAttribute("VideoRemotePort", "The destination port of the video server",
                        UintegerValue(10000),  // 默认视频端口10000
                        MakeUintegerAccessor(&MultiTcpAvStreamClient::GetVideoRemotePort,
                                             &MultiTcpAvStreamClient::SetVideoRemotePort),
                        MakeUintegerChecker<uint16_t>())

          // 音频服务器地址属性
          .AddAttribute("AudioRemoteAddress", "The destination address of the audio server",
                        AddressValue(),  // 默认值为空
                        MakeAddressAccessor(&MultiTcpAvStreamClient::GetAudioRemoteAddress,
                                            &MultiTcpAvStreamClient::SetAudioRemoteAddress),
                        MakeAddressChecker())

          // 音频服务器端口属性
          .AddAttribute("AudioRemotePort", "The destination port of the audio server",
                        UintegerValue(10001),  // 默认音频端口10001
                        MakeUintegerAccessor(&MultiTcpAvStreamClient::GetAudioRemotePort,
                                             &MultiTcpAvStreamClient::SetAudioRemotePort),
                        MakeUintegerChecker<uint16_t>())

          // 段持续时间属性
          .AddAttribute("SegmentDuration", "The duration of a segment in microseconds",
                        UintegerValue(2000000),  // 默认2秒
                        MakeUintegerAccessor(&MultiTcpAvStreamClient::GetSegmentDuration,
                                             &MultiTcpAvStreamClient::SetSegmentDuration),
                        MakeUintegerChecker<uint64_t>())

          // 视频段大小文件路径属性
          .AddAttribute("VideoSegmentSizeFilePath",
                        "The path to the file containing video segment sizes",
                        StringValue("video_bitrates.txt"),  // 默认文件名
                        MakeStringAccessor(&MultiTcpAvStreamClient::m_videoSegmentSizeFilePath),
                        MakeStringChecker())

          // 音频段大小文件路径属性
          .AddAttribute("AudioSegmentSizeFilePath",
                        "The path to the file containing audio segment sizes",
                        StringValue("audio_bitrates.txt"),  // 默认文件名
                        MakeStringAccessor(&MultiTcpAvStreamClient::m_audioSegmentSizeFilePath),
                        MakeStringChecker())

          // 仿真ID属性
          .AddAttribute("SimulationId", "The ID of the current simulation", UintegerValue(0),
                        MakeUintegerAccessor(&MultiTcpAvStreamClient::m_simulationId),
                        MakeUintegerChecker<uint32_t>())

          // 客户端总数属性
          .AddAttribute("NumberOfClients", "The total number of clients", UintegerValue(1),
                        MakeUintegerAccessor(&MultiTcpAvStreamClient::m_numberOfClients),
                        MakeUintegerChecker<uint16_t>())

          // 客户端ID属性
          .AddAttribute("ClientId", "The ID of this client", UintegerValue(0),
                        MakeUintegerAccessor(&MultiTcpAvStreamClient::m_clientId),
                        MakeUintegerChecker<uint32_t>());
  return tid;
}

// 构造函数
MultiTcpAvStreamClient::MultiTcpAvStreamClient() {
  NS_LOG_FUNCTION(this);  // 记录构造函数调用

  // 初始化数据成员
  m_data = 0;
  m_dataSize = 0;
  state = initial;

  // 初始化播放状态
  m_bufferUnderrun = false;
  m_currentPlaybackIndex = 0;
  m_segmentsInBuffer = 0;

  // 初始化流连接状态
  m_videoConnected = false;
  m_audioConnected = false;

  // 初始化段接收状态
  m_videoSegmentReceived = false;
  m_audioSegmentReceived = false;

  // 初始化播放延迟
  m_bDelay = 0;

  // 初始化视频流数据
  m_videoStream.socket = 0;
  m_videoStream.algo = NULL;
  m_videoStream.currentRepIndex = 0;
  m_videoStream.segmentCounter = 0;
  m_videoStream.bytesReceived = 0;
  m_videoStream.type = VIDEO_STREAM;
  m_videoStream.streamType = "video";

  // 初始化音频流数据
  m_audioStream.socket = 0;
  m_audioStream.algo = NULL;
  m_audioStream.currentRepIndex = 0;
  m_audioStream.segmentCounter = 0;
  m_audioStream.bytesReceived = 0;
  m_audioStream.type = AUDIO_STREAM;
  m_audioStream.streamType = "audio";
}

// 初始化客户端
void MultiTcpAvStreamClient::Initialise(std::string algorithm, uint16_t clientId) {
  NS_LOG_FUNCTION(this << algorithm << clientId);

  // 设置视频流段持续时间
  m_videoStream.segmentDuration = m_videoStream.segmentDuration;
  m_audioStream.segmentDuration = m_videoStream.segmentDuration;  // 音频使用相同的段持续时间

  // 读取视频段大小文件
  if (ReadInBitrateValues(m_videoSegmentSizeFilePath, true) == -1) {
    NS_LOG_ERROR("Opening video bitrate file failed. Terminating.");
    Simulator::Stop();
    Simulator::Destroy();
  }

  // 读取音频段大小文件
  if (ReadInBitrateValues(m_audioSegmentSizeFilePath, false) == -1) {
    NS_LOG_ERROR("Opening audio bitrate file failed. Terminating.");
    Simulator::Stop();
    Simulator::Destroy();
  }

  // 设置最后一个段索引
  m_videoStream.lastSegmentIndex = (int64_t)m_videoStream.m_video.segmentSize.at(0).size() - 1;
  m_audioStream.lastSegmentIndex = (int64_t)m_audioStream.m_video.segmentSize.at(0).size() - 1;

  // 设置最大码率索引
  m_videoStream.highestRepIndex = m_videoStream.m_video.averageBitrate.size() - 1;
  m_audioStream.highestRepIndex = m_audioStream.m_video.averageBitrate.size() - 1;

  // 为两个流创建自适应算法对象
  if (algorithm == "tobasco") {
    m_videoStream.algo = new TobascoAlgorithm(m_videoStream.m_video, m_videoStream.m_playback,
                                              m_videoStream.m_buffer, m_videoStream.m_throughput);
    m_audioStream.algo = new TobascoAlgorithm(m_audioStream.m_video, m_audioStream.m_playback,
                                              m_audioStream.m_buffer, m_audioStream.m_throughput);
  } else if (algorithm == "panda") {
    m_videoStream.algo = new PandaAlgorithm(m_videoStream.m_video, m_videoStream.m_playback,
                                            m_videoStream.m_buffer, m_videoStream.m_throughput);
    m_audioStream.algo = new PandaAlgorithm(m_audioStream.m_video, m_audioStream.m_playback,
                                            m_audioStream.m_buffer, m_audioStream.m_throughput);
  } else if (algorithm == "festive") {
    m_videoStream.algo = new FestiveAlgorithm(m_videoStream.m_video, m_videoStream.m_playback,
                                              m_videoStream.m_buffer, m_videoStream.m_throughput);
    m_audioStream.algo = new FestiveAlgorithm(m_audioStream.m_video, m_audioStream.m_playback,
                                              m_audioStream.m_buffer, m_audioStream.m_throughput);
  } else {
    NS_LOG_ERROR("Invalid algorithm name entered. Terminating.");
    StopApplication();
    Simulator::Stop();
    Simulator::Destroy();
  }

  m_algoName = algorithm;  // 保存算法名称

  // 初始化日志文件
  InitializeLogFiles(ToString(m_simulationId), ToString(m_clientId), ToString(m_numberOfClients));
}

// 析构函数
MultiTcpAvStreamClient::~MultiTcpAvStreamClient() {
  NS_LOG_FUNCTION(this);

  // 释放视频流资源
  if (m_videoStream.algo != NULL) {
    delete m_videoStream.algo;
    m_videoStream.algo = NULL;
  }

  // 释放音频流资源
  if (m_audioStream.algo != NULL) {
    delete m_audioStream.algo;
    m_audioStream.algo = NULL;
  }

  // 释放数据缓冲区
  delete[] m_data;
  m_data = 0;
  m_dataSize = 0;
}

// 为指定流请求码率索引
void MultiTcpAvStreamClient::RequestRepIndex(StreamType streamType) {
  NS_LOG_FUNCTION(this << streamType);

  StreamData* streamData = GetStreamData(streamType);  // 获取流数据指针
  if (streamData == NULL) return;                      // 安全检查

  algorithmReply answer;  // 存储算法回复

  // 调用自适应算法
  answer = streamData->algo->GetNextRep(streamData->segmentCounter, m_clientId);
  streamData->currentRepIndex = answer.nextRepIndex;  // 更新当前码率索引

  // 确保码率索引不超过最大值
  NS_ASSERT_MSG(answer.nextRepIndex <= streamData->highestRepIndex,
                "Algorithm returned representation index higher than maximum");

  // 保存播放序列中的码率索引
  streamData->m_playback.playbackIndex.push_back(answer.nextRepIndex);

  // 更新播放延迟（取两个流中的最大值）
  m_bDelay = std::max(m_bDelay, answer.nextDownloadDelay);

  // 记录自适应算法决策
  LogAdaptation(answer, streamType);
}

// 发送数据包到指定流
template <typename T>
void MultiTcpAvStreamClient::Send(T& message, StreamType streamType) {
  NS_LOG_FUNCTION(this << message << streamType);

  StreamData* streamData = GetStreamData(streamType);         // 获取流数据指针
  if (streamData == NULL || streamData->socket == 0) return;  // 安全检查

  PreparePacket(message);  // 准备数据包

  // 创建数据包并发送
  Ptr<Packet> p = Create<Packet>(m_data, m_dataSize);
  streamData->downloadRequestSent = Simulator::Now().GetMicroSeconds();  // 记录发送时间

  NS_LOG_INFO("Sending " << streamData->streamType << " request for " << message << " bytes");
  streamData->socket->Send(p);  // 发送数据包
}

// 处理数据包接收
void MultiTcpAvStreamClient::HandleRead(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);

  // 获取套接字对应的流类型
  StreamType streamType = GetStreamTypeFromSocket(socket);
  StreamData* streamData = GetStreamData(streamType);

  if (streamData == NULL) {
    NS_LOG_WARN("Received data from unknown socket");
    return;
  }

  Ptr<Packet> packet;

  // 如果是当前段的第一个数据包，记录接收开始时间
  if (streamData->bytesReceived == 0) {
    streamData->transmissionStartReceivingSegment = Simulator::Now().GetMicroSeconds();
    NS_LOG_DEBUG(streamData->streamType << " segment reception started");
  }

  uint32_t packetSize;

  // 循环接收所有可用数据包
  while ((packet = socket->Recv())) {
    packetSize = packet->GetSize();  // 获取数据包大小

    // 记录吞吐量日志
    LogThroughput(packetSize, streamType);

    // 累加已接收字节数
    streamData->bytesReceived += packetSize;

    // 获取当前请求的段大小
    int64_t expectedSize = streamData->m_video.segmentSize.at(streamData->currentRepIndex)
                               .at(streamData->segmentCounter);

    // 检查是否已接收完整段
    if (streamData->bytesReceived == expectedSize) {
      NS_LOG_DEBUG(streamData->streamType
                   << " segment received completely: " << streamData->bytesReceived << "/"
                   << expectedSize << " bytes");

      // 更新段接收状态
      if (streamType == VIDEO_STREAM) {
        m_videoSegmentReceived = true;
      } else {
        m_audioSegmentReceived = true;
      }

      // 处理段接收完成
      SegmentReceivedHandle(streamType);
    }
  }
}

// 获取套接字对应的流类型
MultiTcpAvStreamClient::StreamType
MultiTcpAvStreamClient::GetStreamTypeFromSocket(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);

  // 比较套接字指针确定流类型
  if (socket == m_videoStream.socket) {
    return VIDEO_STREAM;
  } else if (socket == m_audioStream.socket) {
    return AUDIO_STREAM;
  } else {
    NS_LOG_WARN("Unknown socket received data");
    return VIDEO_STREAM;  // 默认返回视频流
  }
}

// 获取流数据指针
MultiTcpAvStreamClient::StreamData* MultiTcpAvStreamClient::GetStreamData(StreamType streamType) {
  NS_LOG_FUNCTION(this << streamType);

  // 根据流类型返回对应的数据指针
  if (streamType == VIDEO_STREAM) {
    return &m_videoStream;
  } else if (streamType == AUDIO_STREAM) {
    return &m_audioStream;
  } else {
    return NULL;
  }
}

// 处理段接收完成
void MultiTcpAvStreamClient::SegmentReceivedHandle(StreamType streamType) {
  NS_LOG_FUNCTION(this << streamType);

  StreamData* streamData = GetStreamData(streamType);
  if (streamData == NULL) return;

  // 记录当前时间作为接收完成时间
  streamData->transmissionEndReceivingSegment = Simulator::Now().GetMicroSeconds();

  // 将接收完成时间存入缓冲时间记录数组
  streamData->m_buffer.timeNow.push_back(streamData->transmissionEndReceivingSegment);

  // 处理缓冲区等级计算
  if (streamData->segmentCounter > 0) {
    // 如果不是第一段，计算旧缓冲量
    streamData->m_buffer.bufferLevelOld.push_back(
        std::max(streamData->m_buffer.bufferLevelNew.back() -
                     (streamData->transmissionEndReceivingSegment -
                      streamData->m_throughput.transmissionEnd.back()),
                 (int64_t)0));
  } else {
    // 第一段，旧缓冲量为0
    streamData->m_buffer.bufferLevelOld.push_back(0);
  }

  // 计算新缓冲量
  streamData->m_buffer.bufferLevelNew.push_back(streamData->m_buffer.bufferLevelOld.back() +
                                                streamData->segmentDuration);

  // 记录吞吐量相关信息
  streamData->m_throughput.bytesReceived.push_back(
      streamData->m_video.segmentSize.at(streamData->currentRepIndex)
          .at(streamData->segmentCounter));
  streamData->m_throughput.transmissionStart.push_back(
      streamData->transmissionStartReceivingSegment);
  streamData->m_throughput.transmissionRequested.push_back(streamData->downloadRequestSent);
  streamData->m_throughput.transmissionEnd.push_back(streamData->transmissionEndReceivingSegment);

  // 写入下载日志
  LogDownload(streamType);

  // 写入缓冲日志
  LogBuffer(streamType);

  // 重置已接收字节数
  streamData->bytesReceived = 0;

  // 检查两个流是否都已完成当前段接收
  if (BothSegmentsReceived()) {
    // 缓冲中段数加1（视频和音频算作一个完整的媒体段）
    m_segmentsInBuffer++;

    // 重置段接收状态
    m_videoSegmentReceived = false;
    m_audioSegmentReceived = false;

    // 如果是最后一段，取消延迟
    if (streamData->segmentCounter == streamData->lastSegmentIndex) {
      m_bDelay = 0;
    }

    // 通知Controller下载完成事件
    controllerEvent event = downloadFinished;
    Controller(event);
  }
}

// 读取段大小文件
int MultiTcpAvStreamClient::ReadInBitrateValues(std::string segmentSizeFile, bool isVideo) {
  NS_LOG_FUNCTION(this << segmentSizeFile << isVideo);

  std::ifstream myfile;                  // 文件输入流
  myfile.open(segmentSizeFile.c_str());  // 打开文件

  if (!myfile) {  // 文件打开失败
    NS_LOG_ERROR("Cannot open file: " << segmentSizeFile);
    return -1;
  }

  StreamData* streamData = isVideo ? &m_videoStream : &m_audioStream;  // 获取对应的流数据

  std::string temp;                 // 临时保存每行文本
  int64_t averageByteSizeTemp = 0;  // 临时保存平均字节数

  // 清空现有数据
  streamData->m_video.segmentSize.clear();
  streamData->m_video.averageBitrate.clear();

  // 按行读取文件
  while (std::getline(myfile, temp)) {
    if (temp.empty()) {  // 遇到空行停止
      break;
    }

    // 将一行数据解析为int64_t向量
    std::istringstream buffer(temp);
    std::vector<int64_t> line((std::istream_iterator<int64_t>(buffer)),
                              std::istream_iterator<int64_t>());

    // 保存段大小数据
    streamData->m_video.segmentSize.push_back(line);

    // 计算平均字节数
    averageByteSizeTemp = (int64_t)std::accumulate(line.begin(), line.end(), 0.0) / line.size();

    // 计算并保存平均比特率（bit/s）
    streamData->m_video.averageBitrate.push_back((8.0 * averageByteSizeTemp) /
                                                 (streamData->segmentDuration / 1000000.0));
  }

  // 确保成功读取数据
  NS_ASSERT_MSG(!streamData->m_video.segmentSize.empty(),
                "No segment sizes read from file: " << segmentSizeFile);

  myfile.close();  // 关闭文件

  NS_LOG_INFO("Loaded " << (isVideo ? "video" : "audio") << " bitrate file with "
                        << streamData->m_video.segmentSize.size() << " representations and "
                        << streamData->m_video.segmentSize[0].size() << " segments");

  return 1;
}

// 播放处理函数
bool MultiTcpAvStreamClient::PlaybackHandle() {
  NS_LOG_FUNCTION(this);

  int64_t timeNow = Simulator::Now().GetMicroSeconds();  // 获取当前时间

  // 检查缓冲区下溢
  if (m_segmentsInBuffer == 0 && m_currentPlaybackIndex < m_videoStream.lastSegmentIndex &&
      !m_bufferUnderrun) {
    m_bufferUnderrun = true;  // 标记缓冲区下溢

    // 记录缓冲区下溢开始时间
    bufferUnderrunLog << std::setfill(' ') << std::setw(26) << timeNow / (double)1000000 << " ";
    bufferUnderrunLog.flush();

    return true;  // 返回true表示发生缓冲区下溢
  }
  // 缓冲区中有数据
  else if (m_segmentsInBuffer > 0) {
    if (m_bufferUnderrun) {  // 如果之前有缓冲区下溢，标记恢复
      m_bufferUnderrun = false;
      bufferUnderrunLog << std::setfill(' ') << std::setw(13) << timeNow / (double)1000000 << "\n";
      bufferUnderrunLog.flush();
    }

    // 记录播放开始时间（使用视频流的时间）
    m_videoStream.m_playback.playbackStart.push_back(timeNow);
    m_audioStream.m_playback.playbackStart.push_back(timeNow);

    // 记录播放日志
    LogPlayback(VIDEO_STREAM);
    LogPlayback(AUDIO_STREAM);

    // 更新缓冲区和播放索引
    m_segmentsInBuffer--;
    m_currentPlaybackIndex++;

    return false;  // 返回false表示播放成功
  }

  return true;  // 默认返回true
}

// 设置视频服务器地址和端口（IPv4）
void MultiTcpAvStreamClient::SetVideoRemote(Ipv4Address ip, uint16_t port) {
  NS_LOG_FUNCTION(this << ip << port);
  m_videoStream.peerAddress = Address(ip);  // 转换为通用地址类型
  m_videoStream.peerPort = port;            // 设置端口
}

// 设置音频服务器地址和端口（IPv4）
void MultiTcpAvStreamClient::SetAudioRemote(Ipv4Address ip, uint16_t port) {
  NS_LOG_FUNCTION(this << ip << port);
  m_audioStream.peerAddress = Address(ip);
  m_audioStream.peerPort = port;
}

// 设置视频服务器地址和端口（通用地址）
void MultiTcpAvStreamClient::SetVideoRemote(Address ip, uint16_t port) {
  NS_LOG_FUNCTION(this << ip << port);
  m_videoStream.peerAddress = ip;
  m_videoStream.peerPort = port;
}

// 设置音频服务器地址和端口（通用地址）
void MultiTcpAvStreamClient::SetAudioRemote(Address ip, uint16_t port) {
  NS_LOG_FUNCTION(this << ip << port);
  m_audioStream.peerAddress = ip;
  m_audioStream.peerPort = port;
}

// 检查两个流是否都已连接
bool MultiTcpAvStreamClient::BothStreamsConnected() { return m_videoConnected && m_audioConnected; }

// 检查两个流的当前段是否都已接收完成
bool MultiTcpAvStreamClient::BothSegmentsReceived() {
  return m_videoSegmentReceived && m_audioSegmentReceived;
}

// 释放资源函数
void MultiTcpAvStreamClient::DoDispose(void) {
  NS_LOG_FUNCTION(this);
  Application::DoDispose();  // 调用父类的释放函数
}

// 启动应用程序
void MultiTcpAvStreamClient::StartApplication(void) {
  NS_LOG_FUNCTION(this);

  // 创建视频流套接字
  if (m_videoStream.socket == 0) {
    TypeId tid = TypeId::LookupByName("ns3::TcpSocketFactory");   // 获取TCP套接字工厂
    m_videoStream.socket = Socket::CreateSocket(GetNode(), tid);  // 创建套接字

    // 根据地址类型连接视频服务器
    if (Ipv4Address::IsMatchingType(m_videoStream.peerAddress)) {
      m_videoStream.socket->Connect(InetSocketAddress(
          Ipv4Address::ConvertFrom(m_videoStream.peerAddress), m_videoStream.peerPort));
    } else if (Ipv6Address::IsMatchingType(m_videoStream.peerAddress)) {
      m_videoStream.socket->Connect(Inet6SocketAddress(
          Ipv6Address::ConvertFrom(m_videoStream.peerAddress), m_videoStream.peerPort));
    }

    // 设置视频流连接回调
    m_videoStream.socket->SetConnectCallback(
        MakeCallback(&MultiTcpAvStreamClient::ConnectionSucceeded, this),
        MakeCallback(&MultiTcpAvStreamClient::ConnectionFailed, this));

    // 设置视频流接收回调
    m_videoStream.socket->SetRecvCallback(MakeCallback(&MultiTcpAvStreamClient::HandleRead, this));

    NS_LOG_INFO("Video stream connecting to port " << m_videoStream.peerPort);
  }

  // 创建音频流套接字
  if (m_audioStream.socket == 0) {
    TypeId tid = TypeId::LookupByName("ns3::TcpSocketFactory");
    m_audioStream.socket = Socket::CreateSocket(GetNode(), tid);

    // 根据地址类型连接音频服务器
    if (Ipv4Address::IsMatchingType(m_audioStream.peerAddress)) {
      m_audioStream.socket->Connect(InetSocketAddress(
          Ipv4Address::ConvertFrom(m_audioStream.peerAddress), m_audioStream.peerPort));
    } else if (Ipv6Address::IsMatchingType(m_audioStream.peerAddress)) {
      m_audioStream.socket->Connect(Inet6SocketAddress(
          Ipv6Address::ConvertFrom(m_audioStream.peerAddress), m_audioStream.peerPort));
    }

    // 设置音频流连接回调
    m_audioStream.socket->SetConnectCallback(
        MakeCallback(&MultiTcpAvStreamClient::ConnectionSucceeded, this),
        MakeCallback(&MultiTcpAvStreamClient::ConnectionFailed, this));

    // 设置音频流接收回调
    m_audioStream.socket->SetRecvCallback(MakeCallback(&MultiTcpAvStreamClient::HandleRead, this));

    NS_LOG_INFO("Audio stream connecting to port " << m_audioStream.peerPort);
  }
}

// 停止应用程序
void MultiTcpAvStreamClient::StopApplication() {
  NS_LOG_FUNCTION(this);

  // 关闭视频流套接字
  if (m_videoStream.socket != 0) {
    m_videoStream.socket->Close();
    m_videoStream.socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
    m_videoStream.socket = 0;
  }

  // 关闭音频流套接字
  if (m_audioStream.socket != 0) {
    m_audioStream.socket->Close();
    m_audioStream.socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
    m_audioStream.socket = 0;
  }

  // 关闭所有日志文件
  m_videoStream.downloadLog.close();
  m_videoStream.playbackLog.close();
  m_videoStream.adaptationLog.close();
  m_videoStream.bufferLog.close();
  m_videoStream.throughputLog.close();

  m_audioStream.downloadLog.close();
  m_audioStream.playbackLog.close();
  m_audioStream.adaptationLog.close();
  m_audioStream.bufferLog.close();
  m_audioStream.throughputLog.close();

  bufferUnderrunLog.close();
}

// 准备数据包
template <typename T>
void MultiTcpAvStreamClient::PreparePacket(T& message) {
  NS_LOG_FUNCTION(this << message);

  std::ostringstream ss;
  ss << message;  // 将消息序列化为字符串

  uint32_t dataSize = ss.str().size() + 1;  // 计算数据大小（包含空字符）

  // 如果数据大小变化，重新分配缓冲区
  if (dataSize != m_dataSize) {
    delete[] m_data;
    m_data = new uint8_t[dataSize];
    m_dataSize = dataSize;
  }

  // 复制数据到缓冲区
  memcpy(m_data, ss.str().c_str(), dataSize);

  NS_LOG_DEBUG("Prepared packet with " << dataSize << " bytes: " << ss.str());
}

// 连接成功回调
void MultiTcpAvStreamClient::ConnectionSucceeded(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);

  // 确定是哪个流的连接成功
  if (socket == m_videoStream.socket) {
    m_videoConnected = true;
    NS_LOG_INFO("Video stream connection succeeded");
  } else if (socket == m_audioStream.socket) {
    m_audioConnected = true;
    NS_LOG_INFO("Audio stream connection succeeded");
  }

  // 检查两个流是否都已连接
  if (BothStreamsConnected()) {
    NS_LOG_INFO("Both video and audio streams connected, starting controller");
    controllerEvent event = init;  // 触发初始化事件
    Controller(event);             // 启动控制器
  }
}

// 连接失败回调
void MultiTcpAvStreamClient::ConnectionFailed(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);

  if (socket == m_videoStream.socket) {
    NS_LOG_ERROR("Video stream connection failed");
  } else if (socket == m_audioStream.socket) {
    NS_LOG_ERROR("Audio stream connection failed");
  }
}

// 记录吞吐量日志
void MultiTcpAvStreamClient::LogThroughput(uint32_t packetSize, StreamType streamType) {
  NS_LOG_FUNCTION(this << packetSize << streamType);

  StreamData* streamData = GetStreamData(streamType);
  if (streamData == NULL) return;

  // 写入吞吐量日志
  streamData->throughputLog << std::setfill(' ') << std::setw(13)
                            << Simulator::Now().GetMicroSeconds() / (double)1000000 << " "
                            << std::setfill(' ') << std::setw(13) << packetSize << "\n";
  streamData->throughputLog.flush();
}

// 记录下载日志
void MultiTcpAvStreamClient::LogDownload(StreamType streamType) {
  NS_LOG_FUNCTION(this << streamType);

  StreamData* streamData = GetStreamData(streamType);
  if (streamData == NULL) return;

  // 获取当前段大小
  int64_t segmentSize = streamData->m_video.segmentSize.at(streamData->currentRepIndex)
                            .at(streamData->segmentCounter);

  // 写入下载日志
  streamData->downloadLog << std::setfill(' ') << std::setw(13) << streamData->segmentCounter << " "
                          << std::setfill(' ') << std::setw(21)
                          << streamData->downloadRequestSent / (double)1000000 << " "
                          << std::setfill(' ') << std::setw(14)
                          << streamData->transmissionStartReceivingSegment / (double)1000000 << " "
                          << std::setfill(' ') << std::setw(12)
                          << streamData->transmissionEndReceivingSegment / (double)1000000 << " "
                          << std::setfill(' ') << std::setw(12) << segmentSize << " "
                          << std::setfill(' ') << std::setw(12) << "Y\n";
  streamData->downloadLog.flush();
}

// 记录缓冲区日志
void MultiTcpAvStreamClient::LogBuffer(StreamType streamType) {
  NS_LOG_FUNCTION(this << streamType);

  StreamData* streamData = GetStreamData(streamType);
  if (streamData == NULL) return;

  // 写入缓冲区日志
  streamData->bufferLog << std::setfill(' ') << std::setw(13)
                        << streamData->transmissionEndReceivingSegment / (double)1000000 << " "
                        << std::setfill(' ') << std::setw(13)
                        << streamData->m_buffer.bufferLevelOld.back() / (double)1000000 << "\n"
                        << std::setfill(' ') << std::setw(13)
                        << streamData->transmissionEndReceivingSegment / (double)1000000 << " "
                        << std::setfill(' ') << std::setw(13)
                        << streamData->m_buffer.bufferLevelNew.back() / (double)1000000 << "\n";
  streamData->bufferLog.flush();
}

// 记录自适应算法日志
void MultiTcpAvStreamClient::LogAdaptation(algorithmReply answer, StreamType streamType) {
  NS_LOG_FUNCTION(this << streamType);

  StreamData* streamData = GetStreamData(streamType);
  if (streamData == NULL) return;

  // 写入自适应算法日志
  streamData->adaptationLog << std::setfill(' ') << std::setw(13) << streamData->segmentCounter
                            << " " << std::setfill(' ') << std::setw(9)
                            << streamData->currentRepIndex << " " << std::setfill(' ')
                            << std::setw(22) << answer.decisionTime / (double)1000000 << " "
                            << std::setfill(' ') << std::setw(4) << answer.decisionCase << " "
                            << std::setfill(' ') << std::setw(9) << answer.delayDecisionCase
                            << "\n";
  streamData->adaptationLog.flush();
}

// 记录播放日志
void MultiTcpAvStreamClient::LogPlayback(StreamType streamType) {
  NS_LOG_FUNCTION(this << streamType);

  StreamData* streamData = GetStreamData(streamType);
  if (streamData == NULL) return;

  // 写入播放日志
  streamData->playbackLog << std::setfill(' ') << std::setw(13) << m_currentPlaybackIndex << " "
                          << std::setfill(' ') << std::setw(14)
                          << Simulator::Now().GetMicroSeconds() / (double)1000000 << " "
                          << std::setfill(' ') << std::setw(13)
                          << streamData->m_playback.playbackIndex.at(m_currentPlaybackIndex)
                          << "\n";
  streamData->playbackLog.flush();
}

// 初始化所有日志文件
void MultiTcpAvStreamClient::InitializeLogFiles(std::string simulationId, std::string clientId,
                                                std::string numberOfClients) {
  NS_LOG_FUNCTION(this << simulationId << clientId << numberOfClients);

  // 初始化视频流日志文件
  std::string videoPrefix = dashLogDirectory + m_algoName + "/" + numberOfClients + "/sim" +
                            simulationId + "_cl" + clientId + "_video_";

  // 视频下载日志
  std::string vdLog = videoPrefix + "downloadLog.txt";
  m_videoStream.downloadLog.open(vdLog.c_str());
  m_videoStream.downloadLog << "Segment_Index Download_Request_Sent Download_Start "
                            << "Download_End Segment_Size Download_OK\n";
  m_videoStream.downloadLog.flush();

  // 视频播放日志
  std::string vpLog = videoPrefix + "playbackLog.txt";
  m_videoStream.playbackLog.open(vpLog.c_str());
  m_videoStream.playbackLog << "Segment_Index Playback_Start Quality_Level\n";
  m_videoStream.playbackLog.flush();

  // 视频自适应日志
  std::string vaLog = videoPrefix + "adaptationLog.txt";
  m_videoStream.adaptationLog.open(vaLog.c_str());
  m_videoStream.adaptationLog << "Segment_Index Rep_Level Decision_Point_Of_Time Case DelayCase\n";
  m_videoStream.adaptationLog.flush();

  // 视频缓冲区日志
  std::string vbLog = videoPrefix + "bufferLog.txt";
  m_videoStream.bufferLog.open(vbLog.c_str());
  m_videoStream.bufferLog << "     Time_Now  Buffer_Level \n";
  m_videoStream.bufferLog.flush();

  // 视频吞吐量日志
  std::string vtLog = videoPrefix + "throughputLog.txt";
  m_videoStream.throughputLog.open(vtLog.c_str());
  m_videoStream.throughputLog << "     Time_Now Bytes Received \n";
  m_videoStream.throughputLog.flush();

  // 初始化音频流日志文件
  std::string audioPrefix = dashLogDirectory + m_algoName + "/" + numberOfClients + "/sim" +
                            simulationId + "_cl" + clientId + "_audio_";

  // 音频下载日志
  std::string adLog = audioPrefix + "downloadLog.txt";
  m_audioStream.downloadLog.open(adLog.c_str());
  m_audioStream.downloadLog << "Segment_Index Download_Request_Sent Download_Start "
                            << "Download_End Segment_Size Download_OK\n";
  m_audioStream.downloadLog.flush();

  // 音频播放日志
  std::string apLog = audioPrefix + "playbackLog.txt";
  m_audioStream.playbackLog.open(apLog.c_str());
  m_audioStream.playbackLog << "Segment_Index Playback_Start Quality_Level\n";
  m_audioStream.playbackLog.flush();

  // 音频自适应日志
  std::string aaLog = audioPrefix + "adaptationLog.txt";
  m_audioStream.adaptationLog.open(aaLog.c_str());
  m_audioStream.adaptationLog << "Segment_Index Rep_Level Decision_Point_Of_Time Case DelayCase\n";
  m_audioStream.adaptationLog.flush();

  // 音频缓冲区日志
  std::string abLog = audioPrefix + "bufferLog.txt";
  m_audioStream.bufferLog.open(abLog.c_str());
  m_audioStream.bufferLog << "     Time_Now  Buffer_Level \n";
  m_audioStream.bufferLog.flush();

  // 音频吞吐量日志
  std::string atLog = audioPrefix + "throughputLog.txt";
  m_audioStream.throughputLog.open(atLog.c_str());
  m_audioStream.throughputLog << "     Time_Now Bytes Received \n";
  m_audioStream.throughputLog.flush();

  // 初始化缓冲区下溢日志（共享）
  std::string buLog = dashLogDirectory + m_algoName + "/" + numberOfClients + "/sim" +
                      simulationId + "_cl" + clientId + "_bufferUnderrunLog.txt";
  bufferUnderrunLog.open(buLog.c_str());
  bufferUnderrunLog << "Buffer_Underrun_Started_At         Until \n";
  bufferUnderrunLog.flush();

  NS_LOG_INFO("Log files initialized for client " << clientId);
}

}  // namespace ns3