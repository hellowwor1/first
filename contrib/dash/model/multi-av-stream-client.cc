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

void MultiTcpAvStreamClient::Controller(controllerEvent event,
                                        StreamType type) {
  StreamData& streamdata =
      (type == VIDEO_STREAM ? m_videoStream : m_audioStream);
  NS_LOG_FUNCTION(this << ToStringControllerState(streamdata.state)
                       << ToStringControllerEvent(event)
                       << ToStringStreamType(type));  // 记录函数调用和事件
  // 初始状态处理
  if (streamdata.state == initial) {
    // 为流请求码率索引
    RequestRepIndex(&streamdata);  // 请求视频流码率索引

    // 发送流的下载请求
    // 发送段请求
    Send(streamdata.m_segmentData.segmentSize.at(streamdata.m_currentRepIndex)
             .at(streamdata.m_segmentCounter),
         &streamdata);

    streamdata.state = downloading;  // 切换到下载状态

    return;
  }
  // 下载状态处理
  if (streamdata.state == downloading) {
    PlaybackHandleSingle(streamdata);  // 尝试播放缓冲区中的段
    // 检查是否还有段需要播放
    if (streamdata.m_currentPlaybackIndex <= streamdata.m_lastSegmentIndex) {
      // 流的下载计数器加1
      streamdata.m_segmentCounter++;
      // 为流请求下一段的码率索引
      RequestRepIndex(&streamdata);
      streamdata.state = downloadingPlaying;  // 切换到下载+播放状态
      // 发送流的下一段请求
      Send(streamdata.m_segmentData.segmentSize.at(streamdata.m_currentRepIndex)
               .at(streamdata.m_segmentCounter),
           &streamdata);
    } else {
      // 所有段都已下载，切换到播放状态
      streamdata.state = playing;
    }
    // 调度下一次播放完成事件
    controllerEvent ev = playbackFinished;
    Simulator::Schedule(MicroSeconds(streamdata.m_segmentDuration),
                        &MultiTcpAvStreamClient::Controller, this, ev, type);
    return;
  }
  // 如果当前状态是 downloadingPlaying（下载+播放）
  if (streamdata.state == downloadingPlaying) {
    if (event == downloadFinished) {  // 如果触发事件是下载完成
      if (streamdata.m_segmentCounter <
          streamdata.m_lastSegmentIndex) {  // 如果还有 segment 待下载
        streamdata.m_segmentCounter++;      // 下载计数器 +1
        RequestRepIndex(&streamdata);       // 获取下一段码率索引
      }

      if (streamdata.m_bDelay > 0 &&
          streamdata.m_segmentCounter <= streamdata.m_lastSegmentIndex) {
        /*  e_dirs */  // 延迟下载事件标记
        NS_LOG_FUNCTION(this
                        << "延迟下载事件发生" << streamdata.m_bDelay
                        << ToStringControllerState(streamdata.state)
                        << ToStringControllerEvent(event)
                        << ToStringStreamType(type));  // 记录函数调用和事件
        streamdata.state = playing;                    // 切换到播放状态
        controllerEvent ev = irdFinished;              // 设置事件为延迟下载完成
        // 调度延迟事件触发
        Simulator::Schedule(MicroSeconds(streamdata.m_bDelay),
                            &MultiTcpAvStreamClient::Controller, this, ev,
                            type);
      } else if (streamdata.m_segmentCounter ==
                 streamdata.m_lastSegmentIndex) {  // 如果当前下载最后一段
        /*  e_df */                                // 下载完成标记
        streamdata.state = playing;                // 切换为播放状态
      } else {                                     // 如果还有 segment 待下载
        /*  e_d */                                 // 下载事件标记
        // 发送下一段下载请求
        Send(streamdata.m_segmentData.segmentSize
                 .at(streamdata.m_currentRepIndex)
                 .at(streamdata.m_segmentCounter),
             &streamdata);
      }
    } else if (event == playbackFinished) {     // 如果触发事件是播放完成
      if (!PlaybackHandleSingle(streamdata)) {  // 尝试播放下一段，如果返回
                                                // false 表示缓冲中还有
        // segment
        /*  e_pb */                             // 播放缓冲标记
        controllerEvent ev = playbackFinished;  // 设置播放完成事件
        // 调度下一次播放完成事件
        Simulator::Schedule(MicroSeconds(streamdata.m_segmentDuration),
                            &MultiTcpAvStreamClient::Controller, this, ev,
                            type);
      } else {                           // 缓冲为空，无法播放
        /*  e_pu */                      // 播放空缓冲标记
        streamdata.state = downloading;  // 切换回只下载状态
      }
    }
    return;  // 结束本次 Controller 调用
  }
  // 如果当前状态是 playing（只播放）
  if (streamdata.state == playing) {
    if (event == irdFinished) {               // 如果延迟下载事件完成
      /*  e_irc */                            // 延迟下载完成标记
      streamdata.state = downloadingPlaying;  // 状态切换回下载+播放
      // 发送当前段下载请求
      Send(streamdata.m_segmentData.segmentSize.at(streamdata.m_currentRepIndex)
               .at(streamdata.m_segmentCounter),
           &streamdata);
    } else if (event == playbackFinished && streamdata.m_currentPlaybackIndex <
                                                streamdata.m_lastSegmentIndex) {
      // 如果播放完成，且还有 segment 没有播放完
      /*  e_pb */                             // 播放完成标记
      PlaybackHandleSingle(streamdata);       // 播放缓冲区中的 segment
      controllerEvent ev = playbackFinished;  // 生成播放完成事件
      // 调度下一次播放完成事件
      Simulator::Schedule(MicroSeconds(streamdata.m_segmentDuration),
                          &MultiTcpAvStreamClient::Controller, this, ev, type);
    } else if (event == playbackFinished && streamdata.m_currentPlaybackIndex ==
                                                streamdata.m_lastSegmentIndex) {
      // 如果播放完成，且已经是最后的segment
      PlaybackHandleSingle(streamdata);  // 播放最后一段
      /*  e_pf */                        // 播放完成标记
      streamdata.state = terminal;       // 状态切换为终止
      StopApplication();                 // 停止客户端应用
    }
    return;  // 结束本次 Controller 调用
  }
}
std::string MultiTcpAvStreamClient::ToStringStreamType(StreamType type) {
  switch (type) {
    case VIDEO_STREAM:
      return "video_stream";
    case AUDIO_STREAM:
      return "audio_stream";
    default:
      return "";
  }
}

std::string
MultiTcpAvStreamClient::ToStringControllerEvent(controllerEvent events) {
  switch (events) {
    case downloadFinished:
      return "downloadFinished";
    case playbackFinished:
      return "playbackFinished";
    case irdFinished:
      return "irdFinished";
    case init:
      return "init";
    default:
      return "";
  }
}

std::string
MultiTcpAvStreamClient::ToStringControllerState(controllerState state) {
  switch (state) {
    case initial:
      return "initial";
    case downloading:
      return "downloading";
    case downloadingPlaying:
      return "downloadingPlaying";
    case playing:
      return "playing";
    case terminal:
      return "terminal";
    default:
      return "";
  }
}

void MultiTcpAvStreamClient::PlaybackController(controllerEvent event) {}

// 获取类型标识函数
TypeId MultiTcpAvStreamClient::GetTypeId(void) {
  static TypeId tid =
      TypeId("ns3::MultiTcpAvStreamClient")          // 类型名称
          .SetParent<Application>()                  // 指定父类为Application
          .SetGroupName("Applications")              // 分组名称
          .AddConstructor<MultiTcpAvStreamClient>()  // 添加默认构造函数

          // 视频服务器地址属性
          .AddAttribute("VideoRemoteAddress",
                        "The destination address of the video server",
                        AddressValue(),  // 默认值为空
                        MakeAddressAccessor(
                            &MultiTcpAvStreamClient::GetVideoRemoteAddress,
                            &MultiTcpAvStreamClient::SetVideoRemoteAddress),
                        MakeAddressChecker())

          // 视频服务器端口属性
          .AddAttribute(
              "VideoRemotePort", "The destination port of the video server",
              UintegerValue(10000),  // 默认视频端口10000
              MakeUintegerAccessor(&MultiTcpAvStreamClient::GetVideoRemotePort,
                                   &MultiTcpAvStreamClient::SetVideoRemotePort),
              MakeUintegerChecker<uint16_t>())

          // 音频服务器地址属性
          .AddAttribute("AudioRemoteAddress",
                        "The destination address of the audio server",
                        AddressValue(),  // 默认值为空
                        MakeAddressAccessor(
                            &MultiTcpAvStreamClient::GetAudioRemoteAddress,
                            &MultiTcpAvStreamClient::SetAudioRemoteAddress),
                        MakeAddressChecker())

          // 音频服务器端口属性
          .AddAttribute(
              "AudioRemotePort", "The destination port of the audio server",
              UintegerValue(10001),  // 默认音频端口10001
              MakeUintegerAccessor(&MultiTcpAvStreamClient::GetAudioRemotePort,
                                   &MultiTcpAvStreamClient::SetAudioRemotePort),
              MakeUintegerChecker<uint16_t>())

          // 段持续时间属性
          .AddAttribute(
              "SegmentDuration", "The duration of a segment in microseconds",
              UintegerValue(2000000),  // 默认2秒 ,这里的单位是 微秒
              MakeUintegerAccessor(&MultiTcpAvStreamClient::m_segmentDuration),
              MakeUintegerChecker<uint64_t>())

          // 请求数据的选择
          .AddAttribute(
              "StreamSelection",
              "Which streams the client downloads: 0=video, 1=audio, "
              "2=audio+video",
              EnumValue(VIDEO_ONLY),  // 默认值
              MakeEnumAccessor(&MultiTcpAvStreamClient::m_streamSelection),
              MakeEnumChecker(VIDEO_ONLY, "VideoOnly", AUDIO_ONLY, "AudioOnly",
                              AUDIO_VIDEO, "AudioVideo"))

          // 视频段大小文件路径属性
          .AddAttribute(
              "VideoSegmentSizeFilePath",
              "The path to the file containing video segment sizes",
              StringValue("video_bitrates.txt"),  // 默认文件名
              MakeStringAccessor(
                  &MultiTcpAvStreamClient::m_videoSegmentSizeFilePath),
              MakeStringChecker())

          // 音频段大小文件路径属性
          .AddAttribute(
              "AudioSegmentSizeFilePath",
              "The path to the file containing audio segment sizes",
              StringValue("audio_bitrates.txt"),  // 默认文件名
              MakeStringAccessor(
                  &MultiTcpAvStreamClient::m_audioSegmentSizeFilePath),
              MakeStringChecker())

          // 仿真ID属性
          .AddAttribute(
              "SimulationId", "The ID of the current simulation",
              UintegerValue(0),
              MakeUintegerAccessor(&MultiTcpAvStreamClient::m_simulationId),
              MakeUintegerChecker<uint32_t>())

          // 客户端总数属性
          .AddAttribute(
              "NumberOfClients", "The total number of clients",
              UintegerValue(1),
              MakeUintegerAccessor(&MultiTcpAvStreamClient::m_numberOfClients),
              MakeUintegerChecker<uint16_t>())

          // 客户端ID属性
          .AddAttribute(
              "ClientId", "The ID of this client", UintegerValue(0),
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

  // 初始化流连接状态
  m_videoConnected = false;
  m_audioConnected = false;

  // 初始化视频流数据
  m_videoStream.m_socket = 0;
  m_videoStream.algo = NULL;
  m_videoStream.m_currentRepIndex = 0;
  m_videoStream.m_segmentCounter = 0;
  m_videoStream.m_bytesReceived = 0;
  m_videoStream.m_type = VIDEO_STREAM;
  m_videoStream.m_bufferUnderrun = false;
  m_videoStream.m_currentPlaybackIndex = 0;
  m_videoStream.m_segmentsInBuffer = 0;
  m_videoStream.state = initial;
  m_videoStream.m_SegmentReceived = false;
  m_videoStream.m_bDelay = 0;

  // 初始化音频流数据
  m_audioStream.m_socket = 0;
  m_audioStream.algo = NULL;
  m_audioStream.m_currentRepIndex = 0;
  m_audioStream.m_segmentCounter = 0;
  m_audioStream.m_bytesReceived = 0;
  m_audioStream.m_type = AUDIO_STREAM;
  m_audioStream.m_bufferUnderrun = false;
  m_audioStream.m_currentPlaybackIndex = 0;
  m_audioStream.m_segmentsInBuffer = 0;
  m_audioStream.state = initial;
  m_audioStream.m_SegmentReceived = false;
  m_audioStream.m_bDelay = 0;
}

// 初始化客户端
void MultiTcpAvStreamClient::Initialise(std::string video_algorithm,
                                        std::string audio_algorithm,
                                        uint16_t clientId) {
  NS_LOG_FUNCTION(this << video_algorithm << audio_algorithm << clientId);

  // 设置视频流段持续时间, 音频使用相同的段持续时间
  m_videoStream.m_segmentDuration = m_segmentDuration;
  m_audioStream.m_segmentDuration = m_segmentDuration;

  if (m_streamSelection == VIDEO_ONLY || m_streamSelection == AUDIO_VIDEO) {
    // 读取视频段大小文件
    if (ReadInBitrateValues(m_videoSegmentSizeFilePath, true) == -1) {
      NS_LOG_ERROR("Opening video bitrate file failed. Terminating.");
      Simulator::Stop();
      Simulator::Destroy();
    }
    // 设置最后一个段索引
    m_videoStream.m_lastSegmentIndex =
        (int64_t)m_videoStream.m_segmentData.segmentSize.at(0).size() - 1;
    // 设置最大码率索引
    m_videoStream.m_highestRepIndex =
        m_videoStream.m_segmentData.averageBitrate.size() - 1;

    // 为视频流创建自适应算法对象
    if (video_algorithm == "tobasco") {
      m_videoStream.algo = new TobascoAlgorithm(
          m_videoStream.m_segmentData, m_videoStream.m_playbackData,
          m_videoStream.m_bufferData, m_videoStream.m_throughput);
    } else if (video_algorithm == "panda") {
      m_videoStream.algo = new PandaAlgorithm(
          m_videoStream.m_segmentData, m_videoStream.m_playbackData,
          m_videoStream.m_bufferData, m_videoStream.m_throughput);
    } else if (video_algorithm == "festive") {
      m_videoStream.algo = new FestiveAlgorithm(
          m_videoStream.m_segmentData, m_videoStream.m_playbackData,
          m_videoStream.m_bufferData, m_videoStream.m_throughput);
    } else {
      NS_LOG_ERROR("Invalid video_algorithm name entered. Terminating.");
      StopApplication();
      Simulator::Stop();
      Simulator::Destroy();
    }

    m_videoStream.m_algoName = video_algorithm;  // 保存算法名称
  }
  if (m_streamSelection == AUDIO_ONLY || m_streamSelection == AUDIO_VIDEO) {
    // 读取音频段大小文件
    if (ReadInBitrateValues(m_audioSegmentSizeFilePath, false) == -1) {
      NS_LOG_ERROR("Opening audio bitrate file failed. Terminating.");
      Simulator::Stop();
      Simulator::Destroy();
    }

    m_audioStream.m_lastSegmentIndex =
        (int64_t)m_audioStream.m_segmentData.segmentSize.at(0).size() - 1;

    m_audioStream.m_highestRepIndex =
        m_audioStream.m_segmentData.averageBitrate.size() - 1;

    // // 音频暂不使用 ABR
    // m_audioStream.algo = nullptr;

    // 为音频流创建自适应算法对象
    if (audio_algorithm == "simple") {
      m_audioStream.algo = new AudioSimpleAlgorithm(
          m_audioStream.m_segmentData, m_audioStream.m_playbackData,
          m_audioStream.m_bufferData, m_audioStream.m_throughput);
    } else {
      NS_LOG_ERROR("Invalid audio_algorithm name entered. Terminating.");
      StopApplication();
      Simulator::Stop();
      Simulator::Destroy();
    }
    m_audioStream.m_algoName = audio_algorithm;
  }
  // 初始化各种日志文件
  InitializeLogFiles(ToString(m_simulationId), ToString(m_clientId),
                     ToString(m_numberOfClients));
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

// 为指定流请求码率索引 (目前只有视频支持ABR)
// 2025/12/14 目前音频、视频全部支持ABR
void MultiTcpAvStreamClient::RequestRepIndex(StreamData* streamData) {
  NS_LOG_FUNCTION(this << ToStringStreamType(streamData->m_type));
  algorithmReply answer;  // 存储算法回复
                          // 暂时只为视频动态调整码率
  // 调用自适应算法`
  answer =
      streamData->algo->GetNextRep(streamData->m_segmentCounter, m_clientId);
  streamData->m_currentRepIndex = answer.nextRepIndex;  // 更新当前码率索引

  // 确保码率索引不超过最大值
  NS_ASSERT_MSG(answer.nextRepIndex <= streamData->m_highestRepIndex,
                "Algorithm returned representation index higher than maximum");

  // 保存播放序列中的码率索引，用于后续的日志记录
  streamData->m_playbackData.playbackIndex.push_back(answer.nextRepIndex);
  // 更新播放延迟
  // m_bDelay = std::max(m_bDelay, answer.nextDownloadDelay);
  streamData->m_bDelay = answer.nextDownloadDelay;
  // 记录自适应算法决策
  LogAdaptation(answer, streamData);
}

// 指定流发送数据包到服务器
template <typename T>
void MultiTcpAvStreamClient::Send(T& message, StreamData* streamData) {
  if (streamData->m_socket == 0) return;  // 安全检查
  PreparePacket(message);                 // 准备数据包
  // 创建数据包并发送
  Ptr<Packet> p = Create<Packet>(m_data, m_dataSize);

  NS_LOG_FUNCTION(this << ToStringStreamType(streamData->m_type));

  streamData->m_downloadRequestSent =
      Simulator::Now().GetMicroSeconds();  // 记录发送时间
  streamData->m_socket->Send(p);           // 发送数据包
}

// 处理从服务器接收到的数据
void MultiTcpAvStreamClient::HandleRead(Ptr<Socket> socket) {
  // 获取套接字对应的流类型
  StreamType streamType = GetStreamTypeFromSocket(socket);
  StreamData* streamData = GetStreamData(streamType);

  if (streamData == NULL) {
    NS_LOG_WARN("Received data from unknown socket");
    return;
  }

  Ptr<Packet> packet;

  // 如果是当前段的第一个数据包，记录接收开始时间
  if (streamData->m_bytesReceived == 0) {
    streamData->m_transmissionStartReceivingSegment =
        Simulator::Now().GetMicroSeconds();

    std::string s1 = " " + std::to_string(streamData->m_segmentCounter) + " ";
    NS_LOG_DEBUG(ToStringStreamType(streamType)
                 << s1 << " segment start received(s) : "
                 << streamData->m_transmissionStartReceivingSegment /
                        (double)1000000);
  }

  uint32_t packetSize;  // 保存每个接收到的数据包大小

  // 循环接收所有可用数据包
  while ((packet = socket->Recv())) {
    packetSize = packet->GetSize();  // 获取当前数据包大小（字节数）
    // 记录吞吐量日志
    LogThroughput(packetSize, streamType);
    // 累加已接收字节数
    streamData->m_bytesReceived += packetSize;
    // 获取当前请求的段大小
    int64_t expectedSize =
        streamData->m_segmentData.segmentSize.at(streamData->m_currentRepIndex)
            .at(streamData->m_segmentCounter);
    // 检查是否已接收完整段
    if (streamData->m_bytesReceived == expectedSize) {
      std::string s1 = " " + std::to_string(streamData->m_segmentCounter) + " ";
      NS_LOG_DEBUG(ToStringStreamType(streamData->m_type)
                   << s1 << " segment received completely: "
                   << streamData->m_bytesReceived << "/" << expectedSize
                   << " bytes");
      // 更新段接收状态
      // if (streamType == VIDEO_STREAM) {
      //   m_videoSegmentReceived = true;
      // } else {
      //   m_audioSegmentReceived = true;
      // }
      streamData->m_SegmentReceived = true;

      // 处理段接收完成
      SegmentReceivedHandle(streamType);
    }
  }
}

// 获取套接字对应的流类型
MultiTcpAvStreamClient::StreamType
MultiTcpAvStreamClient::GetStreamTypeFromSocket(Ptr<Socket> socket) {
  // NS_LOG_FUNCTION(this << socket);

  // 比较套接字指针确定流类型
  if (socket == m_videoStream.m_socket) {
    return VIDEO_STREAM;
  } else if (socket == m_audioStream.m_socket) {
    return AUDIO_STREAM;
  } else {
    NS_LOG_WARN("Unknown socket received data");
    return VIDEO_STREAM;  // 默认返回视频流
  }
}

// 获取流数据指针
MultiTcpAvStreamClient::StreamData*
MultiTcpAvStreamClient::GetStreamData(StreamType streamType) {
  // NS_LOG_FUNCTION(this << streamType);

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
  NS_LOG_FUNCTION(this << ToStringStreamType(streamType));

  StreamData* streamData = GetStreamData(streamType);
  if (streamData == NULL) return;

  // 记录当前时间作为接收完成时间
  streamData->m_transmissionEndReceivingSegment =
      Simulator::Now().GetMicroSeconds();

  // 将接收完成时间存入缓冲时间记录数组
  streamData->m_bufferData.timeNow.push_back(
      streamData->m_transmissionEndReceivingSegment);

  // 处理缓冲区等级计算
  if (streamData->m_segmentCounter > 0) {
    /*
      如果不是第一段视频，计算缓冲的“旧缓冲量”
      old buffer level = 上一次缓冲量 - 自上次接收完成以来播放消耗的时间
      如果结果为负数，则取0，保证缓冲量不会为负
      理论来说不会为负数。结果为负数，说明这个流存在卡顿，应当格外注意！
    */
    streamData->m_bufferData.bufferLevelOld.push_back(
        std::max(streamData->m_bufferData.bufferLevelNew.back() -
                     (streamData->m_transmissionEndReceivingSegment -
                      streamData->m_throughput.transmissionEnd.back()),
                 (int64_t)0));
  } else {
    // 第一段，旧缓冲量为0
    streamData->m_bufferData.bufferLevelOld.push_back(0);
  }

  // 计算新缓冲量
  streamData->m_bufferData.bufferLevelNew.push_back(
      streamData->m_bufferData.bufferLevelOld.back() +
      streamData->m_segmentDuration);

  // 记录吞吐量相关信息
  streamData->m_throughput.bytesReceived.push_back(
      streamData->m_segmentData.segmentSize.at(streamData->m_currentRepIndex)
          .at(streamData->m_segmentCounter));  // 本段大小
  streamData->m_throughput.transmissionStart.push_back(
      streamData->m_transmissionStartReceivingSegment);  // 接收开始时间
  streamData->m_throughput.transmissionRequested.push_back(
      streamData->m_downloadRequestSent);  // 请求下载时间
  streamData->m_throughput.transmissionEnd.push_back(
      streamData->m_transmissionEndReceivingSegment);  // 接收结束时间

  // 写入下载日志
  LogDownload(streamType);

  // 写入缓冲日志
  LogBuffer(streamType);

  // 重置已接收字节数
  streamData->m_bytesReceived = 0;
  streamData->m_SegmentReceived = false;

  // 缓冲区里面的段数加1
  streamData->m_segmentsInBuffer++;
  if (streamData->m_segmentCounter == streamData->m_lastSegmentIndex) {
    streamData->m_bDelay = 0;
  }

  // 通知Controller下载完成事件
  controllerEvent event = downloadFinished;
  Controller(event, streamType);
}

// 读取段大小文件
int MultiTcpAvStreamClient::ReadInBitrateValues(std::string segmentSizeFile,
                                                bool isVideo) {
  NS_LOG_FUNCTION(this << segmentSizeFile << (isVideo ? "视频" : "音频"));

  std::ifstream myfile;                  // 文件输入流
  myfile.open(segmentSizeFile.c_str());  // 打开文件

  if (!myfile) {  // 文件打开失败
    NS_LOG_ERROR("Cannot open file: " << segmentSizeFile);
    return -1;
  }

  StreamData* streamData =
      isVideo ? &m_videoStream : &m_audioStream;  // 获取对应的流数据

  std::string temp;                 // 临时保存每行文本
  int64_t averageByteSizeTemp = 0;  // 临时保存平均字节数

  // 清空现有数据
  streamData->m_segmentData.segmentSize.clear();
  streamData->m_segmentData.averageBitrate.clear();

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
    streamData->m_segmentData.segmentSize.push_back(line);

    // 计算平均字节数
    averageByteSizeTemp =
        (int64_t)std::accumulate(line.begin(), line.end(), 0.0) / line.size();

    // 计算并保存平均比特率（bit/s）
    streamData->m_segmentData.averageBitrate.push_back(
        (8.0 * averageByteSizeTemp) /
        (streamData->m_segmentDuration / 1000000.0));
  }

  // 确保成功读取数据
  NS_ASSERT_MSG(!streamData->m_segmentData.segmentSize.empty(),
                "No segment sizes read from file: " << segmentSizeFile);

  myfile.close();  // 关闭文件

  NS_LOG_INFO("Loaded " << (isVideo ? "video" : "audio")
                        << " bitrate file with "
                        << streamData->m_segmentData.segmentSize.size()
                        << " representations and "
                        << streamData->m_segmentData.segmentSize[0].size()
                        << " segments");

  return 1;
}

bool MultiTcpAvStreamClient::PlaybackHandleSingle(StreamData& stream) {
  // 当前模拟时间（微秒）
  int64_t timeNow = Simulator::Now().GetMicroSeconds();
  std::string s1 =
      "播放第 " + std::to_string(stream.m_currentPlaybackIndex) + " 段 ";
  // 如果缓冲区为空且还有剩余段未播放，说明发生缓冲不足（buffer underrun）
  if (stream.m_segmentsInBuffer == 0 &&
      stream.m_currentPlaybackIndex < stream.m_lastSegmentIndex &&
      !stream.m_bufferUnderrun) {
    stream.m_bufferUnderrun = true;  // 标记缓冲不足
    NS_LOG_FUNCTION(s1 << "但是缓存不足无法播放 "
                       << ToStringStreamType(stream.m_type)
                       << timeNow / (double)1000000);  // 日志宏
    // 写入缓冲不足日志：记录开始时间
    stream.bufferUnderrunLog << std::setfill(' ') << std::setw(26)
                             << timeNow / (double)1000000 << " ";
    stream.bufferUnderrunLog.flush();  // 立即刷新到文件
    return true;                       // 返回 true 表示缓冲不足
  }
  // 如果缓冲区中有数据
  else if (stream.m_segmentsInBuffer > 0) {
    if (stream.m_bufferUnderrun) {  // 如果之前缓冲不足，标记已恢复
      stream.m_bufferUnderrun = false;
      stream.bufferUnderrunLog << std::setfill(' ') << std::setw(13)
                               << timeNow / (double)1000000
                               << "\n";  // 记录缓冲恢复时间
      stream.bufferUnderrunLog.flush();
    }

    NS_LOG_FUNCTION(s1 << ToStringStreamType(stream.m_type)
                       << timeNow / (double)1000000);  // 日志宏
    // 将当前播放段开始时间存入播放日志
    stream.m_playbackData.playbackStart.push_back(timeNow);
    LogPlayback(stream.m_type);       // 写入播放日志
    stream.m_segmentsInBuffer--;      // 缓冲区中段数减少
    stream.m_currentPlaybackIndex++;  // 当前播放段索引加1
    return false;                     // 返回 false 表示播放成功
  }

  return true;  // 返回 true 表示已经全部播放完了
}

// 处理多流播放的函数
bool MultiTcpAvStreamClient::PlaybackHandleAV() {
  NS_LOG_FUNCTION(this);

  int64_t timeNow = Simulator::Now().GetMicroSeconds();  // 获取当前时间

  // 必须同时启用并检查缓冲
  bool vEnabled =
      (m_streamSelection == AUDIO_VIDEO || m_streamSelection == VIDEO_ONLY);
  bool aEnabled =
      (m_streamSelection == AUDIO_VIDEO || m_streamSelection == AUDIO_ONLY);

  if (!vEnabled && aEnabled) {
    // 只有音频：降级为单流播放
    return PlaybackHandleSingle(m_audioStream);
  }
  if (!aEnabled && vEnabled) {
    // 只有视频：降级为单流播放
    return PlaybackHandleSingle(m_videoStream);
  }
  // 多流情况
  // 检查两个 buffer 是否都有数据
  // 如果缓冲区为空且还有剩余段未播放，说明发生缓冲不足（buffer underrun）
  // 任何一方为空都不能进行同步播放：记录 underrun 并返回未播放
  if (IsBufferEmpty(VIDEO_STREAM) || IsBufferEmpty(AUDIO_STREAM)) {
    if (IsBufferEmpty(VIDEO_STREAM)) {
      m_videoStream.m_bufferUnderrun = true;
      m_videoStream.bufferUnderrunLog << std::setfill(' ') << std::setw(26)
                                      << timeNow / (double)1000000 << " ";
      m_videoStream.bufferUnderrunLog.flush();

      // 现在视频缓冲区耗尽了，如果音频缓冲区还有则需要记录不此时同步
      if (!IsBufferEmpty(AUDIO_STREAM)) {
      }
    }
    if (IsBufferEmpty(AUDIO_STREAM)) {
      m_audioStream.m_bufferUnderrun = true;
      m_audioStream.bufferUnderrunLog << std::setfill(' ') << std::setw(26)
                                      << timeNow / (double)1000000 << " ";
      m_audioStream.bufferUnderrunLog.flush();

      // 现在音频缓冲区耗尽了，如果视频缓冲区还有则需要记录此时不同步
      if (!IsBufferEmpty(VIDEO_STREAM)) {
      }
    }

    return true;
  }
  // <--------------------------------------------------->
  if (m_videoStream.m_segmentsInBuffer > 0) {
    if (m_videoStream.m_bufferUnderrun) {  // 如果之前缓冲不足，标记已恢复
      m_videoStream.m_bufferUnderrun = false;
      m_videoStream.bufferUnderrunLog << std::setfill(' ') << std::setw(13)
                                      << timeNow / (double)1000000
                                      << "\n";  // 记录缓冲恢复时间
      m_videoStream.bufferUnderrunLog.flush();
    }
  }
  if (m_audioStream.m_segmentsInBuffer > 0) {
    if (m_audioStream.m_bufferUnderrun) {  // 如果之前缓冲不足，标记已恢复
      m_audioStream.m_bufferUnderrun = false;
      m_audioStream.bufferUnderrunLog << std::setfill(' ') << std::setw(13)
                                      << timeNow / (double)1000000
                                      << "\n";  // 记录缓冲恢复时间
      m_audioStream.bufferUnderrunLog.flush();
    }
  }
  if (m_videoStream.m_segmentsInBuffer > 0 &&
      m_audioStream.m_segmentsInBuffer > 0) {
    /*
       音视频缓冲区里面有数据
       统计一个块的出缓冲区时间
     */
    // 记录播放开始时间（使用视频流的时间）
    m_videoStream.m_playbackData.playbackStart.push_back(timeNow);
    m_audioStream.m_playbackData.playbackStart.push_back(timeNow);
  }

  // 记录播放日志
  LogPlayback(VIDEO_STREAM);
  LogPlayback(AUDIO_STREAM);

  // 更新缓冲区和播放索引
  // m_segmentsInBuffer--;
  // m_currentPlaybackIndex++;

  return true;  // 表示所有的块都已经下载并播放完成
}

bool MultiTcpAvStreamClient::IsBufferEmpty(StreamType type) {
  StreamData& streamdata =
      (type == VIDEO_STREAM ? m_videoStream : m_audioStream);
  if (streamdata.m_segmentsInBuffer == 0 &&
      streamdata.m_currentPlaybackIndex < streamdata.m_lastSegmentIndex &&
      !streamdata.m_bufferUnderrun)
    return true;
  else
    return false;
}

// 设置视频服务器地址和端口（IPv4）
void MultiTcpAvStreamClient::SetVideoRemote(Ipv4Address ip, uint16_t port) {
  NS_LOG_FUNCTION(this << Address(ip) << port);
  m_videoStream.m_peerAddress = Address(ip);  // 转换为通用地址类型
  m_videoStream.m_peerPort = port;            // 设置端口
}

// 设置音频服务器地址和端口（IPv4）
void MultiTcpAvStreamClient::SetAudioRemote(Ipv4Address ip, uint16_t port) {
  NS_LOG_FUNCTION(this << Address(ip) << port);
  m_audioStream.m_peerAddress = Address(ip);
  m_audioStream.m_peerPort = port;
}

// 设置视频服务器地址和端口（通用地址）
void MultiTcpAvStreamClient::SetVideoRemote(Address ip, uint16_t port) {
  NS_LOG_FUNCTION(this << ip << port);
  m_videoStream.m_peerAddress = ip;
  m_videoStream.m_peerPort = port;
}

// 设置音频服务器地址和端口（通用地址）
void MultiTcpAvStreamClient::SetAudioRemote(Address ip, uint16_t port) {
  NS_LOG_FUNCTION(this << ip << port);
  m_audioStream.m_peerAddress = ip;
  m_audioStream.m_peerPort = port;
}

// 检查两个流是否都已连接
bool MultiTcpAvStreamClient::BothStreamsConnected() {
  return m_videoConnected && m_audioConnected;
}

// 检查两个流的当前段是否都已接收完成
bool MultiTcpAvStreamClient::BothSegmentsReceived() {
  return m_videoStream.m_SegmentReceived && m_audioStream.m_SegmentReceived;
}

// 释放资源函数
void MultiTcpAvStreamClient::DoDispose(void) {
  NS_LOG_FUNCTION(this);
  Application::DoDispose();  // 调用父类的释放函数
}

// 启动应用程序
void MultiTcpAvStreamClient::StartApplication(void) {
  NS_LOG_FUNCTION(this);

  TypeId tid = TypeId::LookupByName("ns3::TcpSocketFactory");

  // ================================
  // 启动视频流（如果启用）
  // ================================
  if (m_streamSelection == VIDEO_ONLY || m_streamSelection == AUDIO_VIDEO) {
    if (m_videoStream.m_socket == 0) {
      m_videoStream.m_socket = Socket::CreateSocket(GetNode(), tid);

      // 根据地址类型连接远端
      if (Ipv4Address::IsMatchingType(m_videoStream.m_peerAddress)) {
        m_videoStream.m_socket->Connect(InetSocketAddress(
            Ipv4Address::ConvertFrom(m_videoStream.m_peerAddress),
            m_videoStream.m_peerPort));
      } else {
        m_videoStream.m_socket->Connect(Inet6SocketAddress(
            Ipv6Address::ConvertFrom(m_videoStream.m_peerAddress),
            m_videoStream.m_peerPort));
      }

      // 设置连接成功/失败回调
      m_videoStream.m_socket->SetConnectCallback(
          MakeCallback(&MultiTcpAvStreamClient::ConnectionSucceeded, this),
          MakeCallback(&MultiTcpAvStreamClient::ConnectionFailed, this));

      // 设置接收数据回调
      m_videoStream.m_socket->SetRecvCallback(
          MakeCallback(&MultiTcpAvStreamClient::HandleRead, this));

      NS_LOG_INFO("Video stream connecting to port "
                  << m_videoStream.m_peerPort);
    }
  }
  // ================================
  // 启动音频流（如果启用）
  // ================================
  if (m_streamSelection == AUDIO_ONLY || m_streamSelection == AUDIO_VIDEO) {
    if (m_audioStream.m_socket == 0) {
      m_audioStream.m_socket = Socket::CreateSocket(GetNode(), tid);

      if (Ipv4Address::IsMatchingType(m_audioStream.m_peerAddress)) {
        m_audioStream.m_socket->Connect(InetSocketAddress(
            Ipv4Address::ConvertFrom(m_audioStream.m_peerAddress),
            m_audioStream.m_peerPort));
      } else {
        m_audioStream.m_socket->Connect(Inet6SocketAddress(
            Ipv6Address::ConvertFrom(m_audioStream.m_peerAddress),
            m_audioStream.m_peerPort));
      }

      m_audioStream.m_socket->SetConnectCallback(
          MakeCallback(&MultiTcpAvStreamClient::ConnectionSucceeded, this),
          MakeCallback(&MultiTcpAvStreamClient::ConnectionFailed, this));

      m_audioStream.m_socket->SetRecvCallback(
          MakeCallback(&MultiTcpAvStreamClient::HandleRead, this));

      NS_LOG_INFO("Audio stream connecting to port "
                  << m_audioStream.m_peerPort);
    }
  }
}

// 停止应用程序
void MultiTcpAvStreamClient::StopApplication() {
  NS_LOG_FUNCTION(this);

  // 关闭视频流套接字
  if (m_videoStream.m_socket != 0) {
    m_videoStream.m_socket->Close();
    m_videoStream.m_socket->SetRecvCallback(
        MakeNullCallback<void, Ptr<Socket>>());
    m_videoStream.m_socket = 0;
  }

  // 关闭音频流套接字
  if (m_audioStream.m_socket != 0) {
    m_audioStream.m_socket->Close();
    m_audioStream.m_socket->SetRecvCallback(
        MakeNullCallback<void, Ptr<Socket>>());
    m_audioStream.m_socket = 0;
  }

  // 关闭所有日志文件
  m_videoStream.downloadLog.close();
  m_videoStream.playbackLog.close();
  m_videoStream.adaptationLog.close();
  m_videoStream.bufferLog.close();
  m_videoStream.throughputLog.close();
  m_videoStream.bufferUnderrunLog.close();

  m_audioStream.downloadLog.close();
  m_audioStream.playbackLog.close();
  m_audioStream.adaptationLog.close();
  m_audioStream.bufferLog.close();
  m_audioStream.throughputLog.close();
  m_audioStream.bufferUnderrunLog.close();

  // m_avSyncLog.close();
}

// 准备数据包
template <typename T>
void MultiTcpAvStreamClient::PreparePacket(T& message) {
  NS_LOG_FUNCTION(this);

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

  NS_LOG_DEBUG("need packet with " << dataSize - 1
                                   << " 位数(单位字节): " << ss.str());
}

// 连接成功回调
void MultiTcpAvStreamClient::ConnectionSucceeded(Ptr<Socket> socket) {
  StreamType streamType = GetStreamTypeFromSocket(socket);

  NS_LOG_FUNCTION(this << ToStringStreamType(streamType));

  if (streamType == VIDEO_STREAM) {
    m_videoConnected = true;
    NS_LOG_INFO("Video stream connection succeeded");
    StartStreamController(VIDEO_STREAM);

  } else if (streamType == AUDIO_STREAM) {
    m_audioConnected = true;
    NS_LOG_INFO("Audio stream connection succeeded");
    StartStreamController(AUDIO_STREAM);
  }
}

void MultiTcpAvStreamClient::StartStreamController(StreamType type) {
  NS_LOG_FUNCTION(this << ToStringStreamType(type));

  controllerEvent event = init;  // 初始化该流的下载

  // 通知控制器开始调度该流
  Controller(event, type);
}

// 连接失败回调
void MultiTcpAvStreamClient::ConnectionFailed(Ptr<Socket> socket) {
  // NS_LOG_FUNCTION(this << socket);

  if (socket == m_videoStream.m_socket) {
    NS_LOG_ERROR("Video stream connection failed");
  } else if (socket == m_audioStream.m_socket) {
    NS_LOG_ERROR("Audio stream connection failed");
  }
}

// 记录吞吐量日志
void MultiTcpAvStreamClient::LogThroughput(uint32_t packetSize,
                                           StreamType streamType) {
  // NS_LOG_FUNCTION(this << packetSize << ToStringStreamType(streamType));

  StreamData* streamData = GetStreamData(streamType);
  if (streamData == NULL) return;

  // 写入吞吐量日志
  streamData->throughputLog
      << std::setfill(' ') << std::setw(13)
      << Simulator::Now().GetMicroSeconds() / (double)1000000 << " "
      << std::setfill(' ') << std::setw(13) << packetSize << "\n";
  streamData->throughputLog.flush();
}

// 记录下载日志
void MultiTcpAvStreamClient::LogDownload(StreamType streamType) {
  NS_LOG_FUNCTION(this << ToStringStreamType(streamType));

  StreamData* streamData = GetStreamData(streamType);
  if (streamData == NULL) return;

  // 获取当前段大小
  int64_t segmentSize =
      streamData->m_segmentData.segmentSize.at(streamData->m_currentRepIndex)
          .at(streamData->m_segmentCounter);

  // 写入下载日志
  streamData->downloadLog << std::setfill(' ') << std::setw(13)
                          << streamData->m_segmentCounter << " "
                          << std::setfill(' ') << std::setw(21)
                          << streamData->m_downloadRequestSent / (double)1000000
                          << " " << std::setfill(' ') << std::setw(14)
                          << streamData->m_transmissionStartReceivingSegment /
                                 (double)1000000
                          << " " << std::setfill(' ') << std::setw(12)
                          << streamData->m_transmissionEndReceivingSegment /
                                 (double)1000000
                          << " " << std::setfill(' ') << std::setw(12)
                          << segmentSize << " " << std::setfill(' ')
                          << std::setw(12) << "Y\n";
  streamData->downloadLog.flush();
}

// 记录缓冲区日志
void MultiTcpAvStreamClient::LogBuffer(StreamType streamType) {
  NS_LOG_FUNCTION(this << ToStringStreamType(streamType));

  StreamData* streamData = GetStreamData(streamType);
  if (streamData == NULL) return;

  // 写入缓冲区日志
  streamData->bufferLog
      << std::setfill(' ') << std::setw(13)
      << streamData->m_transmissionStartReceivingSegment / (double)1000000
      << " " << std::setfill(' ') << std::setw(13)
      << streamData->m_bufferData.bufferLevelOld.back() / (double)1000000
      << "\n"
      << std::setfill(' ') << std::setw(13)
      << streamData->m_transmissionEndReceivingSegment / (double)1000000 << " "
      << std::setfill(' ') << std::setw(13)
      << streamData->m_bufferData.bufferLevelNew.back() / (double)1000000
      << "\n";
  streamData->bufferLog.flush();
}

// 记录自适应算法日志
void MultiTcpAvStreamClient::LogAdaptation(algorithmReply answer,
                                           StreamData* streamData) {
  NS_LOG_FUNCTION(this << ToStringStreamType(streamData->m_type));

  // 写入自适应算法日志
  streamData->adaptationLog
      << std::setfill(' ') << std::setw(13) << streamData->m_segmentCounter
      << " " << std::setfill(' ') << std::setw(9)
      << streamData->m_currentRepIndex << " " << std::setfill(' ')
      << std::setw(22) << answer.decisionTime / (double)1000000 << " "
      << std::setfill(' ') << std::setw(4) << answer.decisionCase << " "
      << std::setfill(' ') << std::setw(9) << answer.delayDecisionCase << "\n";
  streamData->adaptationLog.flush();
}

// 记录播放日志
void MultiTcpAvStreamClient::LogPlayback(StreamType streamType) {
  NS_LOG_FUNCTION(this << ToStringStreamType(streamType));

  StreamData* streamData = GetStreamData(streamType);
  if (streamData == NULL) return;

  // 写入播放日志
  streamData->playbackLog << std::setfill(' ') << std::setw(13)
                          << streamData->m_currentPlaybackIndex << " "
                          << std::setfill(' ') << std::setw(14)
                          << Simulator::Now().GetMicroSeconds() /
                                 (double)1000000
                          << " " << std::setfill(' ') << std::setw(13)
                          << streamData->m_playbackData.playbackIndex.at(
                                 streamData->m_currentPlaybackIndex)
                          << "\n";
  streamData->playbackLog.flush();
}

// 初始化所有日志文件
void MultiTcpAvStreamClient::InitializeLogFiles(std::string simulationId,
                                                std::string clientId,
                                                std::string numberOfClients) {
  NS_LOG_FUNCTION(this << simulationId << clientId << numberOfClients);

  // 初始化日志文件
  std::string basePrefix =
      dashLogDirectory + "/sim" + simulationId + "_" + "cl" + numberOfClients;

  std::string videoPrefix = basePrefix + "/" + m_videoStream.m_algoName;

  // 初始化音频流日志文件
  std::string audioPrefix = basePrefix + "/" + m_audioStream.m_algoName;

  // 视频下载日志
  std::string vdLog = videoPrefix + "/downloadLog.txt";
  m_videoStream.downloadLog.open(vdLog.c_str());
  m_videoStream.downloadLog
      << "Segment_Index Download_Request_Sent Download_Start "
      << "Download_End Segment_Size Download_OK\n";
  m_videoStream.downloadLog.flush();

  // 视频播放日志
  std::string vpLog = videoPrefix + "/playbackLog.txt";
  m_videoStream.playbackLog.open(vpLog.c_str());
  m_videoStream.playbackLog << "Segment_Index Playback_Start Quality_Level\n";
  m_videoStream.playbackLog.flush();

  // 视频自适应日志
  std::string vaLog = videoPrefix + "/adaptationLog.txt";
  m_videoStream.adaptationLog.open(vaLog.c_str());
  m_videoStream.adaptationLog
      << "Segment_Index Rep_Level Decision_Point_Of_Time Case DelayCase\n";
  m_videoStream.adaptationLog.flush();

  // 视频缓冲区日志
  std::string vbLog = videoPrefix + "/bufferLog.txt";
  m_videoStream.bufferLog.open(vbLog.c_str());
  m_videoStream.bufferLog << "     Time_Now  Buffer_Level \n";
  m_videoStream.bufferLog.flush();

  // 视频吞吐量日志
  std::string vtLog = videoPrefix + "/throughputLog.txt";
  m_videoStream.throughputLog.open(vtLog.c_str());
  m_videoStream.throughputLog << "     Time_Now Bytes Received \n";
  m_videoStream.throughputLog.flush();

  // 视频缓冲区不足日志
  std::string vbuLog = videoPrefix + "/bufferUnderrunLog.txt";
  m_videoStream.bufferUnderrunLog.open(vbuLog.c_str());
  m_videoStream.bufferUnderrunLog
      << "Buffer_Underrun_Started_At         Until \n";
  m_videoStream.bufferUnderrunLog.flush();

  // 音频下载日志
  std::string adLog = audioPrefix + "/downloadLog.txt";
  m_audioStream.downloadLog.open(adLog.c_str());
  m_audioStream.downloadLog
      << "Segment_Index Download_Request_Sent Download_Start "
      << "Download_End Segment_Size Download_OK\n";
  m_audioStream.downloadLog.flush();

  // 音频播放日志
  std::string apLog = audioPrefix + "/playbackLog.txt";
  m_audioStream.playbackLog.open(apLog.c_str());
  m_audioStream.playbackLog << "Segment_Index Playback_Start Quality_Level\n";
  m_audioStream.playbackLog.flush();

  // 音频自适应日志
  std::string aaLog = audioPrefix + "/adaptationLog.txt";
  m_audioStream.adaptationLog.open(aaLog.c_str());
  m_audioStream.adaptationLog
      << "Segment_Index Rep_Level Decision_Point_Of_Time Case DelayCase\n";
  m_audioStream.adaptationLog.flush();

  // 音频缓冲区日志
  std::string abLog = audioPrefix + "/bufferLog.txt";
  m_audioStream.bufferLog.open(abLog.c_str());
  m_audioStream.bufferLog << "     Time_Now  Buffer_Level \n";
  m_audioStream.bufferLog.flush();

  // 音频吞吐量日志
  std::string atLog = audioPrefix + "/throughputLog.txt";
  m_audioStream.throughputLog.open(atLog.c_str());
  m_audioStream.throughputLog << "     Time_Now Bytes Received \n";
  m_audioStream.throughputLog.flush();

  // 音频缓冲区不足日志
  std::string abuLog = audioPrefix + "/bufferUnderrunLog.txt";
  m_audioStream.bufferUnderrunLog.open(abuLog.c_str());
  m_audioStream.bufferUnderrunLog
      << "Buffer_Underrun_Started_At         Until \n";
  m_audioStream.bufferUnderrunLog.flush();

  NS_LOG_INFO("Log files initialized for client " << clientId);
}

}  // namespace ns3