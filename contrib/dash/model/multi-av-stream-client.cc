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

void MultiTcpAvStreamClient::Controller_AV(controllerEvent event,
                                           StreamType type) {
  StreamData& streamdata =
      (type == VIDEO_STREAM ? m_videoStream : m_audioStream);
  NS_LOG_FUNCTION(this << ToStringControllerState(streamdata.state)
                       << ToStringControllerEvent(event)
                       << ToStringStreamType(type));  // 记录函数调用和事件
  NS_LOG_INFO("当前时间: " << Simulator::Now().GetMicroSeconds() /
                                  (double)1000000
                           << "  " << ToStringControllerState(streamdata.state)
                           << "  " << ToStringControllerEvent(event) << "  "
                           << ToStringStreamType(type));  // 记录函数调用和事件
  // 初始状态处理
  if (streamdata.state == initial) {
    NS_LOG_INFO("初始状态，发送第一个段请求");
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
    NS_LOG_INFO("下载状态");
    bool playRes = PlaybackHandleAV(streamdata);  // 尝试播放缓冲区中的段
    // 检查是否还有段需要播放
    if (streamdata.m_currentPlaybackIndex <= streamdata.m_lastSegmentIndex) {
      // 流的下载计数器加1
      streamdata.m_segmentCounter++;
      streamdata.m_bufferData.m_segmentCounter++;
      // 为流请求下一段的码率索引
      RequestRepIndex(&streamdata);
      streamdata.state = downloadingPlaying;  // 切换到下载+播放状态
      // 发送流的下一段请求
      NS_LOG_INFO("请求下一个片段: "
                  << streamdata.m_segmentCounter
                  << "  码率:  " << streamdata.m_currentRepIndex
                  << " 总片段数:  " << streamdata.m_lastSegmentIndex);
      Send(streamdata.m_segmentData.segmentSize.at(streamdata.m_currentRepIndex)
               .at(streamdata.m_segmentCounter),
           &streamdata);
    } else {
      // 所有段都已下载，切换到播放状态
      NS_LOG_INFO("所有段都已下载，切换到播放状态");
      streamdata.state = playing;
    }
    // 调度下一次播放完成事件
    if (!playRes) {
      controllerEvent ev = playbackFinished;
      Simulator::Schedule(MicroSeconds(streamdata.m_segmentDuration),
                          &MultiTcpAvStreamClient::Controller_AV, this, ev,
                          type);
    } else {
      // 因为对方流没有准备好,无法播放 等100ms再试试
      controllerEvent ev = playbackFinished;
      Simulator::Schedule(MicroSeconds(100000),
                          &MultiTcpAvStreamClient::Controller_AV, this, ev,
                          type);
    }
    return;
  }
  // 如果当前状态是 downloadingPlaying（下载+播放）
  if (streamdata.state == downloadingPlaying) {
    NS_LOG_INFO("下载+播放状态");
    if (event == downloadFinished) {  // 如果触发事件是下载完成
      if (streamdata.m_segmentCounter <
          streamdata.m_lastSegmentIndex) {  // 如果还有 segment 待下载
        streamdata.m_segmentCounter++;      // 下载计数器 +1
        streamdata.m_bufferData.m_segmentCounter++;
        RequestRepIndex(&streamdata);  // 获取下一段码率索引
      }
      // 如果缓冲区状况良好，就停止一段时间下载
      if (streamdata.m_bDelay > 0 &&
          streamdata.m_segmentCounter <= streamdata.m_lastSegmentIndex) {
        NS_LOG_INFO("延迟下载事件发生,延迟:  "
                    << streamdata.m_bDelay / (double)1000000 << "  当前时间: "
                    << Simulator::Now().GetMicroSeconds() / (double)1000000
                    << "  " << ToStringControllerState(streamdata.state) << "  "
                    << ToStringControllerEvent(event) << "  "
                    << ToStringStreamType(type));  // 记录函数调用和事件
        streamdata.state = playing;                // 切换到播放状态
        controllerEvent ev = irdFinished;          // 设置事件为延迟下载完成
        streamdata.m_sbd.isSleeping = true;
        // 调度延迟事件触发
        Simulator::Schedule(MicroSeconds(streamdata.m_bDelay),
                            &MultiTcpAvStreamClient::Controller_AV, this, ev,
                            type);
      } else if (streamdata.m_segmentCounter ==
                 streamdata.m_lastSegmentIndex) {  // 如果当前下载最后一段
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
      if (!PlaybackHandleAV(streamdata)) {      // 尝试播放下一段，如果返回
                                                // false 表示缓冲中还有segment
        /*  e_pb */                             // 播放缓冲标记
        controllerEvent ev = playbackFinished;  // 设置播放完成事件
        // 调度下一次播放完成事件
        Simulator::Schedule(MicroSeconds(streamdata.m_segmentDuration),
                            &MultiTcpAvStreamClient::Controller_AV, this, ev,
                            type);
      } else {  // 缓冲为空， 或者 另一个流没有准备好，无法播放
        if (streamdata.m_segmentsInBuffer > 0) {
          // 另一个流没有准备好，无法播放
          // 等 100ms再试试
          controllerEvent ev = playbackFinished;
          Simulator::Schedule(MicroSeconds(100000),
                              &MultiTcpAvStreamClient::Controller_AV, this, ev,
                              type);
        } else {
          // 确实是本流没有数据导致播放失败了，切换回下载状态，等待下载
          streamdata.state = downloading;  // 切换回只下载状态
        }
      }
    }
    return;  // 结束本次 Controller 调用
  }
  // 如果当前状态是 playing（只播放）
  if (streamdata.state == playing) {
    NS_LOG_INFO("播放状态");
    if (event == irdFinished) {  // 如果延迟下载事件完成
      streamdata.m_sbd.isSleeping = false;
      // 发送当前段下载请求
      NS_LOG_INFO("延迟下载请求结束,当前时间: "
                  << Simulator::Now().GetMicroSeconds() / (double)1000000);
      // 每次“延迟下载请求”结束后，都需要再判断一下是否还是需要再一次延迟
      // 调用自适应算法,获取是否需要延迟下载以及延迟时间
      algorithmReply answer =
          streamdata.algo->GetNextRep(streamdata.m_segmentCounter, m_clientId);
      streamdata.m_bDelay = answer.nextDownloadDelay;  // 更新延迟下载时间
      NS_LOG_INFO("获取到的延迟下载时间: " << streamdata.m_bDelay /
                                                  (double)1000000);
      if (streamdata.m_bDelay > 0 &&
          streamdata.m_segmentCounter < streamdata.m_lastSegmentIndex) {
        NS_LOG_INFO("再次发生延迟下载事件,延迟:  "
                    << streamdata.m_bDelay / (double)1000000 << "  当前时间: "
                    << Simulator::Now().GetMicroSeconds() / (double)1000000
                    << "  " << ToStringControllerState(streamdata.state) << "  "
                    << ToStringControllerEvent(event) << "  "
                    << ToStringStreamType(type));  // 记录函数调用和事件
        streamdata.state = playing;                // 切换到播放状态
        controllerEvent ev = irdFinished;          // 设置事件为延迟下载完成
        streamdata.m_sbd.isSleeping = true;
        // 调度延迟事件触发
        Simulator::Schedule(MicroSeconds(streamdata.m_bDelay),
                            &MultiTcpAvStreamClient::Controller_AV, this, ev,
                            type);
      } else {
        // 没有延迟下载事件了
        streamdata.state = downloadingPlaying;  // 状态切换回下载+播放
        // 发送段请求
        Send(streamdata.m_segmentData.segmentSize
                 .at(streamdata.m_currentRepIndex)
                 .at(streamdata.m_segmentCounter),
             &streamdata);
      }
    } else if (event == playbackFinished && streamdata.m_currentPlaybackIndex <
                                                streamdata.m_lastSegmentIndex) {
      // 如果播放完成，且还有 segment 没有播放完
      bool playRes = PlaybackHandleAV(streamdata);  // 播放缓冲区中的 segment
      if (!playRes) {
        controllerEvent ev = playbackFinished;  // 生成播放完成事件
        // 调度下一次播放完成事件
        Simulator::Schedule(MicroSeconds(streamdata.m_segmentDuration),
                            &MultiTcpAvStreamClient::Controller_AV, this, ev,
                            type);
      } else {
        // 因为对方流没有准备好,无法播放 等100ms再试试
        if (streamdata.m_segmentsInBuffer > 0) {
          controllerEvent ev = playbackFinished;
          Simulator::Schedule(MicroSeconds(100000),
                              &MultiTcpAvStreamClient::Controller_AV, this, ev,
                              type);
        }
      }
    } else if (event == playbackFinished && streamdata.m_currentPlaybackIndex ==
                                                streamdata.m_lastSegmentIndex) {
      // 如果播放完成，且已经是最后的segment
      PlaybackHandleAV(streamdata);  // 播放最后一段
      /*  e_pf */                    // 播放完成标记
      streamdata.state = terminal;   // 状态切换为终止
      StopApplication();             // 停止客户端应用
    }
    return;  // 结束本次 Controller 调用
  }
}

std::string MultiTcpAvStreamClient::ToStringStreamType(StreamType type) {
  switch (type) {
    case VIDEO_STREAM:
      return "视频流";
    case AUDIO_STREAM:
      return "音频流";
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
              MakeUintegerChecker<uint32_t>())

          // 添加采样Rtt的采样周期，默认100ms
          .AddAttribute(
              "RttSampleInterval",
              "RTT sampling interval for shared bottleneck detection",
              TimeValue(MilliSeconds(100)),
              MakeTimeAccessor(&MultiTcpAvStreamClient::m_RttSampleInterval),
              MakeTimeChecker())

          // 添加采样最大窗口，默认500个，也就是最近50s的RTT数据
          .AddAttribute(
              "MaxRttSamples", "Maximum number of RTT samples kept per stream",
              UintegerValue(500),
              MakeUintegerAccessor(&MultiTcpAvStreamClient::m_maxRttSamples),
              MakeUintegerChecker<uint32_t>())

          // 开启共享瓶颈感知的ABR算法设计
          .AddAttribute(
              "EnableSharedBottleneckAwareAbr",
              "Enable ABR adaptation that is aware of shared bottlenecks "
              "between flows",
              BooleanValue(false),
              MakeBooleanAccessor(
                  &MultiTcpAvStreamClient::m_enableSharedBottleneckAwareAbr),
              MakeBooleanChecker())

          // 开启 MPTCP-SBD（INFOCOM 2016） 算法支持
          .AddAttribute(
              "EnableMptcpSbd", "Enable MPTCP-SBD algorithm support",
              BooleanValue(false),
              MakeBooleanAccessor(&MultiTcpAvStreamClient::m_enableMptcpSbd),
              MakeBooleanChecker());

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
  m_videoStream.m_LatestRtt = MilliSeconds(0);
  m_videoStream.m_bufferData.qoe =
      &qoe;  // 将视频流的 QoE 引用指向客户端的 QoE 变量
  m_videoStream.m_bufferData.isAudio = false;  // 视频流的 isAudio 标记为 false

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
  m_audioStream.m_LatestRtt = MilliSeconds(0);
  m_audioStream.m_bufferData.qoe =
      &qoe;  // 将音频流的 QoE 引用指向客户端的 QoE 变量
  m_audioStream.m_bufferData.isAudio = true;  // 音频流的 isAudio 标记为 true

  // 可动态调整
  // m_rttSampleInterval = MilliSeconds(100);  // 默认 100ms
}

// 初始化客户端
void MultiTcpAvStreamClient::Initialise(std::string video_algorithm,
                                        std::string audio_algorithm,
                                        uint16_t clientId) {
  NS_LOG_FUNCTION(this << video_algorithm << audio_algorithm << clientId);

  NS_LOG_INFO("m_segmentationDuration: " << m_segmentDuration);
  // 设置视频流段持续时间, 音频使用相同的段持续时间
  m_videoStream.m_segmentDuration = m_segmentDuration;
  m_videoStream.m_bufferData.m_segmentDuration = m_segmentDuration;
  m_audioStream.m_segmentDuration = m_segmentDuration;
  m_audioStream.m_bufferData.m_segmentDuration = m_segmentDuration;

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
    } else if (video_algorithm == "bba") {
      m_videoStream.algo = new BbaAlgorithm(
          m_videoStream.m_segmentData, m_videoStream.m_playbackData,
          m_videoStream.m_bufferData, m_videoStream.m_throughput);
    } else if (video_algorithm == "sabba") {
      m_videoStream.algo = new SabbaAlgorithm(
          m_videoStream.m_segmentData, m_videoStream.m_playbackData,
          m_videoStream.m_bufferData, m_audioStream.m_bufferData,
          m_videoStream.m_throughput, m_audioStream.m_throughput);
    } else if (video_algorithm == "abr") {
      m_videoStream.algo = new AbrAlgorithm(
          m_videoStream.m_segmentData, m_audioStream.m_segmentData,
          m_videoStream.m_playbackData, m_audioStream.m_playbackData,
          m_videoStream.m_bufferData, m_audioStream.m_bufferData,
          m_videoStream.m_throughput, m_audioStream.m_throughput);
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
    if (audio_algorithm == "festive") {
      m_audioStream.algo = new FestiveAlgorithm(
          m_audioStream.m_segmentData, m_audioStream.m_playbackData,
          m_audioStream.m_bufferData, m_audioStream.m_throughput);
    } else if (audio_algorithm == "bba") {
      m_audioStream.algo = new BbaAlgorithm(
          m_audioStream.m_segmentData, m_audioStream.m_playbackData,
          m_audioStream.m_bufferData, m_audioStream.m_throughput);
    } else if (audio_algorithm == "sabba") {
      m_audioStream.algo = new SabbaAlgorithm(
          m_audioStream.m_segmentData, m_audioStream.m_playbackData,
          m_audioStream.m_bufferData, m_videoStream.m_bufferData,
          m_audioStream.m_throughput, m_videoStream.m_throughput);
    } else if (audio_algorithm == "abr") {
      m_audioStream.algo = new AbrAlgorithm(
          m_audioStream.m_segmentData, m_videoStream.m_segmentData,
          m_audioStream.m_playbackData, m_videoStream.m_playbackData,
          m_audioStream.m_bufferData, m_videoStream.m_bufferData,
          m_audioStream.m_throughput, m_videoStream.m_throughput);
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
  NS_LOG_INFO("请求码率索引: " << answer.nextRepIndex << "  延迟下载时间: "
                               << answer.nextDownloadDelay / (double)1000000);
  streamData->m_bDelay = answer.nextDownloadDelay;
  streamData->m_currentRepIndex = answer.nextRepIndex;  // 更新当前码率索引
  // 保存播放序列中的码率索引，用于后续的日志记录
  streamData->m_playbackData.playbackIndex.push_back(answer.nextRepIndex);
  // 记录自适应算法决策
  LogAdaptation(answer, streamData);
}
void MultiTcpAvStreamClient::DumpOwdWindows(const StreamData& stream,
                                            int64_t timeNow) const {
  std::string streamName =
      stream.m_type == VIDEO_STREAM ? "Video Stream" : "Audio Stream";
  NS_LOG_INFO("========== " << streamName << "==========");
  NS_LOG_INFO("========== OWD Sliding Window Dump ==========");
  NS_LOG_INFO("Now time          : " << timeNow << " ms");
  NS_LOG_INFO("CurrentWindowEnd  : " << stream.m_sbd.m_NowWindowEnd << " ms");
  NS_LOG_INFO("Window count      : " << stream.m_sbd.m_Owds.size());
  NS_LOG_INFO("PB                : " << stream.m_sbd.GetPB());
  NS_LOG_INFO("MeanSkew          : " << stream.m_sbd.GetMeanSkew());
  NS_LOG_INFO("MeanVar           : " << stream.m_sbd.GetMeanVar());
  NS_LOG_INFO("MeanFreq          : " << stream.m_sbd.GetMeanFreq());
  NS_LOG_INFO(
      "GrowthSimilarity_1: " << stream.m_sbd.GetGrowthSimilarity().first);
  NS_LOG_INFO(
      "GrowthSimilarity_2: " << stream.m_sbd.GetGrowthSimilarity().second);
  size_t idx = 0;
  for (const auto& w : stream.m_sbd.m_Owds) {
    int64_t window_end = w.m_NowWindowEnd;
    int64_t window_start = window_end - WINDOW_MS;
    size_t sample_cnt = w.owds.size();

    int64_t min_owd = 0;
    int64_t max_owd = 0;
    double mean_owd = 0.0;

    if (!w.owds.empty()) {
      min_owd = w.owds[0];
      max_owd = w.owds[0];
      int64_t sum = 0;

      for (auto owd : w.owds) {
        sum += owd;
        if (owd < min_owd) min_owd = owd;
        if (owd > max_owd) max_owd = owd;
      }
      mean_owd = static_cast<double>(sum) / w.owds.size();
    }

    NS_LOG_INFO("  [" << idx << "] "
                      << "Window [" << window_start << ", " << window_end
                      << ") ms"
                      << ", samples = " << sample_cnt << ", min = " << min_owd
                      << ", max = " << max_owd << ", mean = " << mean_owd
                      << ", gap: " << w.gap);

    ++idx;
  }

  NS_LOG_INFO("=============================================");
}

void MultiTcpAvStreamClient::AddOwdSample(StreamData& stream, int64_t timeNow,
                                          int64_t owd) {
  // 1. 初始化（第一次调用）
  if (stream.m_sbd.m_NowWindowEnd == 0) {
    stream.m_sbd.m_NowWindowEnd = ((timeNow / WINDOW_MS) + 1) * WINDOW_MS;

    stream.m_sbd.m_Owds.emplace_back(
        OwdInfo{std::vector<int64_t>{}, stream.m_sbd.m_NowWindowEnd, 0.0});
  }
  bool advanced = false;
  // 2. 推进窗口（处理时间跳跃）
  // 如果当前时间不在窗口内，说明是新窗口，在m_Owds末尾添加一个新的vector
  while (timeNow >= stream.m_sbd.m_NowWindowEnd) {
    // 有窗口变更，打印信息
    advanced = true;

    // 窗口刚刚结束，还没推进
    if (!stream.m_sbd.m_Owds.empty()) {
      const OwdInfo& lastWindow = stream.m_sbd.m_Owds.back();
      LogOwdWindow(stream, lastWindow);
    }
    /*
      当前流不处于睡眠状态，并且当前窗口内没有OWD样本
      说明这个窗口是完全没有信息的，网络状态太差导致端侧长时间接收不到数据包
      如果这个窗口不是第一个窗口，那么就复制前一个窗口OWD信息，并且每一个owd都加上一个WINDOW_MS
      如果是第一个窗口就不管
    */
    // if (!stream.m_sbd.isSleeping && stream.m_sbd.m_Owds.back().owds.empty())
    // {
    //   // 至少需要两个窗口（当前 + 前一个）
    //   if (stream.m_sbd.m_Owds.size() >= 2) {
    //     auto& curr = stream.m_sbd.m_Owds.back();
    //     const auto& prev = stream.m_sbd.m_Owds[stream.m_sbd.m_Owds.size() -
    //     2];
    //     // 复制前一个窗口 OWD
    //     curr.owds = prev.owds;
    //     // 每个 OWD 增加 WINDOW_MS
    //     for (auto& owd : curr.owds) {
    //       owd += WINDOW_MS;
    //     }
    //   }
    // }
    // 计算当前彻底不会有新信息进入的窗口内的统计信息,也就是计算当前最后一个窗口的统计信息

    stream.m_sbd.AddMeanOwd();

    /*
     更新streamData->m_NowWindowEnd，将m_NowWindowEnd以350ms为步长前进，前进到要超过timeNow的最小值
    */
    stream.m_sbd.m_NowWindowEnd += WINDOW_MS;
    stream.m_sbd.m_Owds.emplace_back(
        OwdInfo{std::vector<int64_t>{}, stream.m_sbd.m_NowWindowEnd, 0.0});

    // 3. 清理过期窗口（超过 17.5s）
    /*
    在每次要新添加一个vector之前，先判断最旧的vector是否已经超时了，
    一个vector存储350ms的数据，一共保存50个vector，也就是存储17.5s的数据
    逻辑就是判断即将要新添加的这个vector的时间点，是否已经超过了最旧的vector的时间17.5s，循环把这些超出的vector删除掉

    或者换一个逻辑，一共保存50个vector，如果超过了50个，就删除头部的vector.
    */
    // while (!stream.m_sbd.m_Owds.empty()) {
    //   int64_t newest_end = stream.m_sbd.m_NowWindowEnd;
    //   int64_t oldest_end = stream.m_sbd.m_Owds.front().m_NowWindowEnd;

    //   if (newest_end - oldest_end > MAX_RANGE_MS) {
    //     stream.m_sbd.DeleteMeanOwd();
    //     stream.m_sbd.m_Owds.pop_front();
    //   } else {
    //     break;
    //   }
    // }
    while (stream.m_sbd.m_Owds.size() > MAX_WINDOWS) {
      stream.m_sbd.DeleteMeanOwd();
      stream.m_sbd.m_Owds.pop_front();
    }
  }

  // 4. 插入当前 OWD 到最后一个窗口
  /*
    如果当前时间在窗口内，说明是同一个窗口，直接把owd添加到m_Owds最后一个vector中
    如果没有vector就需要添加
  */
  stream.m_sbd.m_Owds.back().owds.push_back(owd);

  if (advanced) {
    // DumpOwdWindows(stream, timeNow);
  }
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
  std::string s1 = " " + std::to_string(streamData->m_segmentCounter) + " ";
  if (streamData == NULL) {
    NS_LOG_WARN("Received data from unknown socket");
    return;
  }

  Ptr<Packet> packet;

  // 如果是当前段的第一个数据包，记录接收开始时间
  if (streamData->m_bytesReceived == 0) {
    streamData->m_transmissionStartReceivingSegment =
        Simulator::Now().GetMicroSeconds();

    NS_LOG_DEBUG(ToStringStreamType(streamType)
                 << s1 << " segment start received(s) : "
                 << streamData->m_transmissionStartReceivingSegment /
                        (double)1000000);
  }

  uint32_t packetSize;  // 保存每个接收到的数据包大小

  // 循环接收所有可用数据包
  while ((packet = socket->Recv())) {
    // 读取包上的 FlowTag
    // FlowTag tag;
    // if (packet->FindFirstMatchingByteTag(tag)) {
    //   // 处理 FlowTag
    //   Time sendTime = tag.GetTxTime();
    //   int64_t owd = (Simulator::Now() - sendTime).GetMilliSeconds();
    //   AddOwdSample(*streamData, Simulator::Now().GetMilliSeconds(), owd);
    // }

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
    // NS_LOG_INFO(ToStringStreamType(streamData->m_type)
    //             << s1 << "段接收情况： " << streamData->m_bytesReceived <<
    //             "/"
    //             << expectedSize << " bytes");

    if (streamData->m_bytesReceived == expectedSize) {
      NS_LOG_DEBUG(ToStringStreamType(streamData->m_type)
                   << s1 << " segment received completely: "
                   << streamData->m_bytesReceived << "/" << expectedSize
                   << " bytes");
      // 更新段接收状态
      streamData->m_SegmentReceived = true;
      // 处理段接收完成
      SegmentReceivedHandle(streamType);

      // 更新QoE
      if (qoe_R[streamData->m_segmentCounter] != 0) {
        NS_LOG_INFO("Updating QoE for segment "
                    << streamData->m_segmentCounter << ": expected size = "
                    << expectedSize / 1000000.0 << " MB, other R = "
                    << qoe_R[streamData->m_segmentCounter] / 1000000.0
                    << " MB");
        qoe_Ds += std::abs(expectedSize - qoe_R[streamData->m_segmentCounter]) /
                  1000000.0;  // 以MB为单位累加质量损失
        qoe_R[streamData->m_segmentCounter] +=
            expectedSize / 1000000.0;  // 以MB为单位累加码率
        qoe_Rs += qoe_R[streamData->m_segmentCounter];
        qoe = (qoe_Rs - qoe_alpha * qoe_Ts - qoe_beta * qoe_Ds);
        NS_LOG_INFO("QoE updated: " << qoe << " (R: " << qoe_Rs
                                    << " Mbps, T: " << qoe_Ts
                                    << " s, D: " << qoe_Ds << " Mbps)");
      } else {
        qoe_R[streamData->m_segmentCounter] =
            expectedSize / 1000000.0;  // 以MB为单位记录码率
      }
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
MultiTcpAvStreamClient::StreamData*
MultiTcpAvStreamClient::GetOtherStreamData(StreamType streamType) {
  if (streamType == VIDEO_STREAM)
    return &m_audioStream;
  else
    return &m_videoStream;
}

// 处理段接收完成
void MultiTcpAvStreamClient::SegmentReceivedHandle(StreamType streamType) {
  NS_LOG_FUNCTION(this << ToStringStreamType(streamType));

  StreamData* streamData = GetStreamData(streamType);
  if (streamData == NULL) return;

  // 记录当前时间作为接收完成时间
  int64_t timeNow = Simulator::Now().GetMicroSeconds();
  streamData->m_transmissionEndReceivingSegment = timeNow;
  NS_LOG_INFO("当前时间： "
              << streamData->m_transmissionEndReceivingSegment / 1e6);
  // 将接收完成时间存入缓冲时间记录数 组
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
    /*
      在多流播放的系统里面，这一部分还存在问题
      “  old buffer level = 上一次缓冲量 - 自上次接收完成以来播放消耗的时间 ”
      这是不对的，因为“上次接收完成以来播放消耗的时间”不再等于上次传输结束时间-这次传输结束时间
      因为中间可能因为对方流缓冲不足而停止播放，因此每个流播放一段，缓冲区就减去一段这样精确
      因此在播放函数里面统计缓冲区水平,问题是如果可以播放，在播放过程中下载完成了，如何统计缓冲区水平呢？
    */
    // streamData->m_bufferData.bufferLevelOld.push_back(
    //     std::max(streamData->m_bufferData.bufferLevelNew.back() -
    //                  (streamData->m_transmissionEndReceivingSegment -
    //                   streamData->m_throughput.transmissionEnd.back()),
    //              (int64_t)0));
    streamData->m_bufferData.bufferLevelOld.push_back(
        streamData->m_segmentsInBuffer * streamData->m_segmentDuration);
    NS_LOG_INFO(ToStringStreamType(streamType) + "旧 缓冲区: "
                << streamData->m_bufferData.bufferLevelOld.back());
  } else {
    // 第一段，旧缓冲量为0
    streamData->m_bufferData.bufferLevelOld.push_back(0);
    NS_LOG_INFO(ToStringStreamType(streamType) + "旧 缓冲区: "
                << streamData->m_bufferData.bufferLevelOld.back());
  }

  // 计算新缓冲量 = 旧缓冲量+新接受的段持续时间
  // streamData->m_bufferData.bufferLevelNew.push_back(
  //     streamData->m_bufferData.bufferLevelOld.back() +
  //     streamData->m_segmentDuration);
  streamData->m_bufferData.bufferLevelNew.push_back(
      (streamData->m_segmentsInBuffer + 1) * streamData->m_segmentDuration);
  NS_LOG_INFO(ToStringStreamType(streamType) + "新 缓冲区: "
              << streamData->m_bufferData.bufferLevelNew.back());
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
  // LogBuffer(streamType);

  // 重置已接收字节数
  streamData->m_bytesReceived = 0;
  streamData->m_SegmentReceived = false;
  streamData->m_ptsQueue.push((streamData->m_segmentCounter + 1) *
                              streamData->m_segmentDuration / 1e6);
  std::string streamName = streamData->m_type == 0 ? "视频流 " : "音频流 ";
  NS_LOG_INFO(streamName << " 的 pts队列数据 "
                         << streamData->m_ptsQueue.back());
  // 缓冲区里面的段数加1
  streamData->m_segmentsInBuffer++;
  streamData->m_bufferData.m_segmentsInBuffer++;

  // 记录缓冲区里面的每一块的大小
  streamData->m_bufferData.segmentSizes.push_back(
      streamData->m_segmentData.segmentSize.at(streamData->m_currentRepIndex)
          .at(streamData->m_segmentCounter));  // 本段大小

  int64_t bufferMs =
      streamData->m_bufferData.m_segmentsInBuffer *
      (streamData->m_segmentDuration / 1000);  // 当前缓冲区水平（毫秒）
  int64_t nowMs = Simulator::Now().GetMilliSeconds();

  UpdateBufferState(nowMs, bufferMs, streamData->m_bufferTrendState);

  // 写入缓冲日志
  LogBuffer_v2(streamType, timeNow);

  if (streamData->m_segmentCounter == streamData->m_lastSegmentIndex) {
    streamData->m_bDelay = 0;
  }

  // 通知Controller下载完成事件
  controllerEvent event = downloadFinished;
  // Controller(event, streamType);
  Controller_AV(event, streamType);
}

// 在网络拓扑脚本处有新Owd到来时，实时更新
void MultiTcpAvStreamClient::NotifyOwd(bool isVideo, int64_t rxTime,
                                       int64_t owd) {
  // NS_LOG_INFO("应用程序准备更新Owd: " << isVideo << ":  " << owd << "  at  "
  //                                     << rxTime);
  if (isVideo) {
    AddOwdSample(m_videoStream, rxTime, owd);
  } else {
    AddOwdSample(m_audioStream, rxTime, owd);
  }
}

// 接收ECN标志函数
void MultiTcpAvStreamClient::NotifyEcn(Ipv4Address srcIp, uint16_t srcPort,
                                       Time rxTime) {
  EcnEvent ev;
  ev.srcIp = srcIp;
  ev.srcPort = srcPort;
  ev.rxTime = rxTime;

  m_ecnEvents.push_back(ev);

  NS_LOG_INFO("应用程序接收到了 ECN 标志: " << srcIp << ":" << srcPort << "at "
                                            << rxTime.GetSeconds());
  m_sbCc.OnCeEvent(srcIp, srcPort, rxTime);

  if (m_sbCc.IsSharedBottleneck()) {
    NS_LOG_UNCOND("[APP] Audio/Video shared bottleneck detected at "
                  << rxTime.GetSeconds() << "s");
    if (preTimeLog_sbcc != (int)Simulator::Now().GetSeconds()) {
      preTimeLog_sbcc = (int)Simulator::Now().GetSeconds();
      m_SbCcLog << (int)Simulator::Now().GetSeconds() << "    " << "1"
                << "\n";
      m_SbCcLog.flush();
    }
  }
}

// trace函数只更新每次rtt状态
void MultiTcpAvStreamClient::VideoRttTrace(Time oldRtt, Time newRtt) {
  m_videoStream.m_LatestRtt = newRtt;
  int64_t timeNow = Simulator::Now().GetMicroSeconds() / (double)1000000;
  NS_LOG_INFO("[RTT]: " << newRtt.GetMilliSeconds() << "   time:" << timeNow);
}
//  trace函数只更新每次rtt状态
void MultiTcpAvStreamClient::AudioRttTrace(Time oldRtt, Time newRtt) {
  m_audioStream.m_LatestRtt = newRtt;
  int64_t timeNow = Simulator::Now().GetMicroSeconds() / (double)1000000;
  NS_LOG_INFO("[RTT]: " << newRtt.GetMilliSeconds() << "   time:" << timeNow);
}

// 读取段大小文件
int MultiTcpAvStreamClient::ReadInBitrateValues(std::string segmentSizeFile,
                                                bool isVideo) {
  std::string info = isVideo ? "视频" : "音频";
  NS_LOG_FUNCTION(this << segmentSizeFile << info);

  std::ifstream myfile;                  // 文件输入流
  myfile.open(segmentSizeFile.c_str());  // 打开文件

  if (!myfile) {  // 文件打开失败
    NS_LOG_ERROR("Cannot open file: " << segmentSizeFile);
    return -1;
  }

  StreamData* streamData =
      isVideo ? &m_videoStream : &m_audioStream;  // 获取对应的流数据

  std::string temp;  // 临时保存每行文本
  // int64_t averageByteSizeTemp = 0;  // 临时保存平均字节数

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
    // averageByteSizeTemp =
    //     (int64_t)std::accumulate(line.begin(), line.end(), 0.0) /
    //     line.size();

    // // 计算并保存平均比特率（bit/s）
    // streamData->m_segmentData.averageBitrate.push_back(
    //     (8.0 * averageByteSizeTemp) /
    //     (streamData->m_segmentDuration / 1000000.0));
    double totalBytes = std::accumulate(line.begin(), line.end(), 0.0);
    double totalDuration =
        line.size() * (streamData->m_segmentDuration / 1000000.0);

    double avgBitrate = 0.0;
    if (!line.empty() && totalDuration > 0.0) {
      avgBitrate = totalBytes * 8.0 / totalDuration;
    }

    streamData->m_segmentData.averageBitrate.push_back(avgBitrate);
  }
  // 将块的平均比特率输出到日志中
  for (double i : streamData->m_segmentData.averageBitrate) {
    NS_LOG_INFO(info << "  averageBitrate(Mbps): " << i / 1e6);
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
  std::string s1 = ToStringStreamType(stream.m_type) + " 播放第 " +
                   std::to_string(stream.m_currentPlaybackIndex) + " 段 ";
  // 如果缓冲区为空且还有剩余段未播放，说明发生缓冲不足（buffer underrun）
  if (stream.m_segmentsInBuffer == 0 &&
      stream.m_currentPlaybackIndex < stream.m_lastSegmentIndex &&
      !stream.m_bufferUnderrun) {
    stream.m_bufferUnderrun = true;  // 标记缓冲不足
    NS_LOG_INFO(s1 << " 缓存不足无法播放 "
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

    NS_LOG_INFO(s1 << ToStringStreamType(stream.m_type)
                   << timeNow / (double)1000000);  // 日志宏
    // 将当前播放段开始时间存入播放日志
    stream.m_playbackData.playbackStart.push_back(timeNow);
    LogPlayback(stream.m_type);   // 写入播放日志
    stream.m_segmentsInBuffer--;  // 缓冲区中段数减少
    stream.m_bufferData.m_segmentsInBuffer--;

    stream.m_bufferData.segmentSizes.pop_front();  // 移除已播放段的大小记录

    int64_t bufferMs =
        stream.m_bufferData.m_segmentsInBuffer *
        (stream.m_segmentDuration / 1000);  // 当前缓冲区水平（毫秒）
    int64_t nowMs = Simulator::Now().GetMilliSeconds();
    UpdateBufferState(nowMs, bufferMs, stream.m_bufferTrendState);

    stream.m_currentPlaybackIndex++;  // 当前播放段索引加1
    return false;                     // 返回 false 表示播放成功
  }

  return true;  // 返回 true 表示已经全部播放完了
}

// 音频+视频 buffer 都有数据才会返回true
bool MultiTcpAvStreamClient::CanPlayAvTogether() {
  NS_LOG_FUNCTION(this);
  bool res = true;
  NS_LOG_INFO("视频 流的 m_currentPlaybackIndex: "
              << m_videoStream.m_currentPlaybackIndex
              << "   m_segmentsInBuffer:" << m_videoStream.m_segmentsInBuffer
              << "  m_bufferUnderrun: " << m_videoStream.m_bufferUnderrun);
  NS_LOG_INFO("音频 流的 m_currentPlaybackIndex: "
              << m_audioStream.m_currentPlaybackIndex
              << "   m_segmentsInBuffer:" << m_audioStream.m_segmentsInBuffer
              << "  m_bufferUnderrun: " << m_audioStream.m_bufferUnderrun);
  // 视频 buffer 必须有
  if (m_videoStream.m_currentPlaybackIndex ==
      m_audioStream.m_currentPlaybackIndex) {
    if (m_videoStream.m_segmentsInBuffer == 0 &&
        m_videoStream.m_currentPlaybackIndex <
            m_videoStream.m_lastSegmentIndex &&
        !m_videoStream.m_bufferUnderrun) {
      m_videoStream.m_bufferUnderrun = true;
      res = false;
    }
    // 音频 buffer 必须有
    if (m_audioStream.m_segmentsInBuffer == 0 &&
        m_audioStream.m_currentPlaybackIndex <
            m_audioStream.m_lastSegmentIndex &&
        !m_audioStream.m_bufferUnderrun) {
      m_audioStream.m_bufferUnderrun = true;
      res = false;
    }
  }
  NS_LOG_INFO("视频 流的 m_currentPlaybackIndex: "
              << m_videoStream.m_currentPlaybackIndex
              << "   m_segmentsInBuffer:" << m_videoStream.m_segmentsInBuffer
              << "  m_bufferUnderrun: " << m_videoStream.m_bufferUnderrun);
  NS_LOG_INFO("音频 流的 m_currentPlaybackIndex: "
              << m_audioStream.m_currentPlaybackIndex
              << "   m_segmentsInBuffer:" << m_audioStream.m_segmentsInBuffer
              << "  m_bufferUnderrun: " << m_audioStream.m_bufferUnderrun);

  return res;
}

bool MultiTcpAvStreamClient::PlaybackHandleAV(StreamData& stream) {
  NS_LOG_FUNCTION(this);
  int64_t timeNow = Simulator::Now().GetMicroSeconds();
  std::string streamName = stream.m_type == VIDEO_STREAM ? "视频" : "音频";
  StreamData* r_stream =
      stream.m_type == VIDEO_STREAM ? &m_audioStream : &m_videoStream;
  std::string s1 = ToStringStreamType(stream.m_type) + " 尝试播放第 " +
                   std::to_string(stream.m_currentPlaybackIndex) + " 段 ";
  NS_LOG_INFO(s1);
  if (!CanPlayAvTogether()) {
    if (stream.m_bufferUnderrun) {
      NS_LOG_INFO(streamName
                  << "段 " << stream.m_currentPlaybackIndex
                  << " 缓存不足无法播放(已记录日志，缓存不足开始时间) "
                  << timeNow / (double)1000000);  // 日志宏
      // 写入缓冲不足日志：记录开始时间

      stream.bufferUnderrunLog << std::setfill(' ') << std::setw(26)
                               << timeNow / (double)1000000 << " ";
      stream.bufferUnderrunLog.flush();            // 立即刷新到文件
      stream.m_bufferUnderrunStartTime = timeNow;  // 记录缓冲不足开始时间
    }
    if (r_stream->m_bufferUnderrun) {
      // 对方流缓冲不足，不能播放
      std::string r_streamName =
          VIDEO_STREAM == stream.m_type ? "对方音频流" : "对方视频流";
      NS_LOG_INFO(r_streamName
                  << "段 " << stream.m_currentPlaybackIndex
                  << " 缓存不足无法播放(已记录日志，缓存不足开始时间) "
                  << timeNow / (double)1000000);  // 日志宏
      r_stream->bufferUnderrunLog << std::setfill(' ') << std::setw(26)
                                  << timeNow / (double)1000000 << " ";
      r_stream->bufferUnderrunLog.flush();            // 立即刷新到文件
      r_stream->m_bufferUnderrunStartTime = timeNow;  // 记录缓冲不足开始时间
    }
    return true;  // 缓冲不足,无法播放
  } else if (stream.m_segmentsInBuffer > 0) {
    if (stream.m_bufferUnderrun) {  // 如果之前缓冲不足，标记已恢复
      stream.m_bufferUnderrun = false;
      NS_LOG_INFO(streamName
                  << "段 " << stream.m_currentPlaybackIndex
                  << " 缓存足够，可以恢复(已记录日志，缓存不足结束时间) "
                  << timeNow / (double)1000000);  // 日志宏
      stream.bufferUnderrunLog << std::setfill(' ') << std::setw(13)
                               << timeNow / (double)1000000
                               << "\n";  // 记录缓冲恢复时间
      stream.bufferUnderrunLog.flush();
      qoe_Ts += (timeNow - stream.m_bufferUnderrunStartTime) / 1000000.0;
      NS_LOG_INFO("当前qoe_Ts: " << qoe_Ts << "s");
    }

    // 获取当前段的 PTS
    videoPts = std::max(videoPts, m_videoStream.m_ptsQueue.empty()
                                      ? 0
                                      : m_videoStream.m_ptsQueue.front());
    audioPts = std::max(audioPts, m_audioStream.m_ptsQueue.empty()
                                      ? 0
                                      : m_audioStream.m_ptsQueue.front());
    int64_t currentPts = stream.m_ptsQueue.front();
    NS_LOG_INFO("currentPts " << currentPts << "  videoPts " << videoPts
                              << "  audioPts " << audioPts);  // 日志宏
    // 统一按最小 PTS 播放，保证 AV 同步
    int64_t playPts = std::min(videoPts, audioPts);
    // 播放
    if (currentPts <= playPts) {
      stream.m_playbackData.playbackStart.push_back(timeNow);

      NS_LOG_INFO(streamName << "段 " << stream.m_currentPlaybackIndex
                             << "可以播放 "
                             << timeNow / (double)1000000);  // 日志宏
      LogPlayback(stream.m_type);                            // 写入播放日志
      stream.m_segmentsInBuffer--;
      stream.m_bufferData.m_segmentsInBuffer--;

      stream.m_bufferData.segmentSizes.pop_front();  // 移除已播放段的大小记录

      int64_t bufferMs =
          stream.m_bufferData.m_segmentsInBuffer *
          (stream.m_segmentDuration / 1000);  // 当前缓冲区水平（毫秒）
      int64_t nowMs = Simulator::Now().GetMilliSeconds();

      UpdateBufferState(nowMs, bufferMs, stream.m_bufferTrendState);

      // 缓冲区变化记录
      LogBuffer_v2(stream.m_type, timeNow);
      NS_LOG_INFO(streamName << " 流播放 " << stream.m_currentPlaybackIndex
                             << " 缓冲区减少变为 "
                             << (stream.m_bufferData.m_segmentsInBuffer *
                                 stream.m_bufferData.m_segmentDuration) /
                                    (double)1e6);
      stream.m_currentPlaybackIndex++;
      stream.m_ptsQueue.pop();
      // 将这个播放时间统计到位，用来计算缓冲区长度

      return false;  // 返回 false 表示播放成功
    } else {
      // 因为pts过大，无法播放
      NS_LOG_INFO("段 " << stream.m_currentPlaybackIndex
                        << " 比另一个流播放的块多了， 无法播放 "
                        << timeNow / (double)1000000);  // 日志宏
    }
  }
  return true;
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

// 释放资源函数
void MultiTcpAvStreamClient::DoDispose(void) {
  NS_LOG_FUNCTION(this);
  Application::DoDispose();  // 调用父类的释放函数
}

void MultiTcpAvStreamClient::VideoCwndChange(uint32_t oldCwnd,
                                             uint32_t newCwnd) {
  NS_LOG_INFO("Video At time " << Simulator::Now().GetSeconds()
                               << "s cwnd changed from " << oldCwnd / 1448
                               << " to " << newCwnd / 1448);
}
void MultiTcpAvStreamClient::AudioCwndChange(uint32_t oldCwnd,
                                             uint32_t newCwnd) {
  NS_LOG_INFO("Audio At time " << Simulator::Now().GetSeconds()
                               << "s cwnd changed from " << oldCwnd / 1448
                               << " to " << newCwnd / 1448);
}
void MultiTcpAvStreamClient::VideoBytesInFlightTrace(uint32_t oldBytes,
                                                     uint32_t newBytes) {
  NS_LOG_INFO("Video At time " << Simulator::Now().GetSeconds()
                               << "s  BytesInFlight: " << oldBytes << " -> "
                               << newBytes);
}
void MultiTcpAvStreamClient::AudioBytesInFlightTrace(uint32_t oldBytes,
                                                     uint32_t newBytes) {
  NS_LOG_INFO("Audio At time " << Simulator::Now().GetSeconds()
                               << "s  BytesInFlight: " << oldBytes << " -> "
                               << newBytes);
}
void MultiTcpAvStreamClient::VideoRtxTrace(SequenceNumber32 seq) {
  NS_LOG_INFO("Video At time " << Simulator::Now().GetSeconds()
                               << " RTO retransmit seq=" << seq);
}
void MultiTcpAvStreamClient::AudioRtxTrace(SequenceNumber32 seq) {
  NS_LOG_INFO("Audio At time " << Simulator::Now().GetSeconds()
                               << " RTO retransmit seq=" << seq);
}
void MultiTcpAvStreamClient::VideoOwdTrace(Time owd, SequenceNumber32 txSeq,
                                           uint32_t payloadSize,
                                           bool isRetrans) {
  // NS_LOG_INFO(Simulator::Now().GetSeconds()
  //             << "s Video OWD: seq=" << txSeq << " size=" << payloadSize
  //             << " retrans=" << isRetrans << " owd=" << owd.GetMilliSeconds()
  //             << " ms");
  AddOwdSample(m_videoStream, Simulator::Now().GetMilliSeconds(),
               owd.GetMilliSeconds());
}
void MultiTcpAvStreamClient::AudioOwdTrace(Time owd, SequenceNumber32 txSeq,
                                           uint32_t payloadSize,
                                           bool isRetrans) {
  // NS_LOG_INFO(Simulator::Now().GetSeconds()
  //             << "s Audio OWD: seq=" << txSeq << " size=" << payloadSize
  //             << " retrans=" << isRetrans << " owd=" << owd.GetMilliSeconds()
  //             << " ms");
  AddOwdSample(m_audioStream, Simulator::Now().GetMilliSeconds(),
               owd.GetMilliSeconds());
}

void MultiTcpAvStreamClient::UpdateBufferState(int64_t nowMs, int64_t bufferMs,
                                               BufferTrendState& s) {
  // ---------- 1) 低缓冲 ----------
  if (bufferMs <= LOW_TH_MS) {
    if (s.lowStartMs < 0) s.lowStartMs = nowMs;

    s.isLow = (nowMs - s.lowStartMs >= PERSIST_TH_MS);
  } else {
    s.lowStartMs = -1;
    s.isLow = false;
  }

  // ---------- 2) 严重低缓冲 ----------
  if (bufferMs <= SEVERE_LOW_TH_MS) {
    if (s.severeLowStartMs < 0) s.severeLowStartMs = nowMs;

    s.isSevereLow = (nowMs - s.severeLowStartMs >= PERSIST_TH_MS);
  } else {
    s.severeLowStartMs = -1;
    s.isSevereLow = false;
  }

  // ---------- 3) 持续走低 ----------
  if (s.lastBufferMs >= 0) {
    int64_t delta = bufferMs - s.lastBufferMs;

    if (delta < -EPS_MS) {
      // 明显下降
      if (s.declineStartMs < 0) s.declineStartMs = s.lastTimeMs;

      s.isDeclining = (nowMs - s.declineStartMs >= PERSIST_TH_MS);
    } else if (delta > EPS_MS) {
      // 明显恢复
      s.declineStartMs = -1;
      s.isDeclining = false;
    } else {
      // 基本不变
      if (s.declineStartMs >= 0) {
        s.isDeclining = (nowMs - s.declineStartMs >= PERSIST_TH_MS);
      }
    }
  }

  s.lastBufferMs = bufferMs;
  s.lastTimeMs = nowMs;
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
      NS_LOG_INFO("Video stream connecting to port "
                  << m_videoStream.m_peerPort);
      // 设置接收数据回调
      m_videoStream.m_socket->SetRecvCallback(
          MakeCallback(&MultiTcpAvStreamClient::HandleRead, this));

      /*
        下面这些全部不准，在网络拓扑连接设置那里调整才是对的
      */
      // 设置Rtt回调
      // m_videoStream.m_socket->TraceConnectWithoutContext(
      //     "RTT", MakeCallback(&MultiTcpAvStreamClient::VideoRttTrace, this));

      // // 设置 TCP拥塞发送窗口回调
      // Ptr<TcpSocketBase> tcpSock =
      //     DynamicCast<TcpSocketBase>(m_videoStream.m_socket);

      // tcpSock->TraceConnectWithoutContext(
      //     "CongestionWindow",
      //     MakeCallback(&MultiTcpAvStreamClient::VideoCwndChange, this));

      // tcpSock->TraceConnectWithoutContext(
      //     "BytesInFlight",
      //     MakeCallback(&MultiTcpAvStreamClient::VideoBytesInFlightTrace,
      //     this));

      // // 设置重传回调
      // tcpSock->TraceConnectWithoutContext(
      //     "Retransmission",
      //     MakeCallback(&MultiTcpAvStreamClient::VideoRtxTrace, this));

      // 开始定期采样Rtt
      // Simulator::Schedule(m_RttSampleInterval,
      //                     &MultiTcpAvStreamClient::SampleVideoRtt, this);
      // NS_LOG_INFO("Video stream 开始定期采样Rtt "
      //             << "采样周期" << m_RttSampleInterval << "采样最大窗口"
      //             << m_maxRttSamples);
      m_videoStream.m_socket->TraceConnectWithoutContext(
          "Owd", MakeCallback(&MultiTcpAvStreamClient::VideoOwdTrace, this));
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

      NS_LOG_INFO("Audio stream connecting to port "
                  << m_audioStream.m_peerPort);

      m_audioStream.m_socket->SetRecvCallback(
          MakeCallback(&MultiTcpAvStreamClient::HandleRead, this));

      // m_audioStream.m_socket->TraceConnectWithoutContext(
      //     "RTT", MakeCallback(&MultiTcpAvStreamClient::AudioRttTrace, this));

      // 开始定期采样Rtt
      // Simulator::Schedule(m_RttSampleInterval,
      //                     &MultiTcpAvStreamClient::SampleAudioRtt, this);

      // NS_LOG_INFO("Audio stream 开始定期采样Rtt "
      //             << "采样周期" << m_RttSampleInterval << "采样最大窗口"
      //             << m_maxRttSamples);

      // Ptr<TcpSocketBase> tcpSock =
      //     DynamicCast<TcpSocketBase>(m_audioStream.m_socket);

      // tcpSock->TraceConnectWithoutContext(
      //     "CongestionWindow",
      //     MakeCallback(&MultiTcpAvStreamClient::AudioCwndChange, this));
      // tcpSock->TraceConnectWithoutContext(
      //     "BytesInFlight",
      //     MakeCallback(&MultiTcpAvStreamClient::AudioBytesInFlightTrace,
      //     this));
      // tcpSock->TraceConnectWithoutContext(
      //     "Retransmission",
      //     MakeCallback(&MultiTcpAvStreamClient::AudioRtxTrace, this));
      m_audioStream.m_socket->TraceConnectWithoutContext(
          "Owd", MakeCallback(&MultiTcpAvStreamClient::AudioOwdTrace, this));
    }
  }
  m_sbCc.SetWindow(MilliSeconds(350));
  m_sbCc.SetAudioFlow(Ipv4Address::ConvertFrom(m_audioStream.m_peerAddress),
                      m_audioStream.m_peerPort);
  m_sbCc.SetVideoFlow(Ipv4Address::ConvertFrom(m_videoStream.m_peerAddress),
                      m_videoStream.m_peerPort);
  // 每隔一段时间就检测一次共享瓶颈
  // Simulator::Schedule(Seconds(2.0), &MultiTcpAvStreamClient::DetectSbd,
  // this);
  // 每个 350ms 检测一次共享瓶颈
  Simulator::Schedule(Seconds(0.350),
                      &MultiTcpAvStreamClient::SharedBottleneckDetected, this);
}

void MultiTcpAvStreamClient::SampleOneStreamRtt(StreamData& stream) {
  Time now = Simulator::Now();
  // NS_LOG_INFO(ToStringStreamType(stream.m_type)
  //             << "采样Rtt: " << stream.m_LatestRtt.GetMilliSeconds()
  //             << "  系统时间：" << now.GetMicroSeconds() / (double)1000000);
  if (!stream.m_LatestRtt.IsZero()) {
    RttEvent ev;
    ev.rxTime = now;
    ev.rtt = stream.m_LatestRtt.GetMilliSeconds();
    stream.m_Rtts.push_back(ev);

    // 限制队列长度（非常重要）
    // 100ms 一次采样，10次也就是1s，窗口定为50s，也就是500次采样。
    if (stream.m_Rtts.size() > m_maxRttSamples) stream.m_Rtts.pop_front();
  }
}

void MultiTcpAvStreamClient::SampleVideoRtt() {
  SampleOneStreamRtt(m_videoStream);

  Simulator::Schedule(m_RttSampleInterval,
                      &MultiTcpAvStreamClient::SampleVideoRtt, this);
}

void MultiTcpAvStreamClient::SampleAudioRtt() {
  SampleOneStreamRtt(m_audioStream);

  Simulator::Schedule(m_RttSampleInterval,
                      &MultiTcpAvStreamClient::SampleAudioRtt, this);
}

// 旧版（粗糙版本）共享瓶颈检测函数
void MultiTcpAvStreamClient::DetectSbd() {
  // 没有开启检查功能，就不执行相关逻辑
  if (!m_enableSharedBottleneckAwareAbr) return;
  m_sbd.UpdataMaxLag(m_videoStream.m_Rtts, m_audioStream.m_Rtts,
                     m_RttSampleInterval, m_maxRttSamples);
  std::pair<bool, double> res =
      m_sbd.Detect(m_videoStream.m_Rtts, m_audioStream.m_Rtts);
  NS_LOG_INFO("相似度：" << res.second << "  窗口偏移大小"
                         << m_sbd.GetMaxLag());

  if (res.first) {
    NS_LOG_INFO("成功进入共享瓶颈状态");
    // 调整 ABR / chunk size
    m_videoStream.m_bufferData.isInSB = true;
    m_audioStream.m_bufferData.isInSB = true;
  } else {
    m_videoStream.m_bufferData.isInSB = false;
    m_audioStream.m_bufferData.isInSB = false;
  }

  Simulator::Schedule(Seconds(2.0), &MultiTcpAvStreamClient::DetectSbd, this);
}

void MultiTcpAvStreamClient::DumpSbdFlags(const std::deque<bool>& flags,
                                          bool currentInSbd) const {
  NS_LOG_INFO("========== MPTCP-SBD Detection ==========");
  NS_LOG_INFO("Current inSbd     : " << currentInSbd);
  NS_LOG_INFO("SBD Flags count   : " << flags.size());
  NS_LOG_INFO("SBD Flags window  :");

  int idx = 0;
  int trueCount = 0;
  for (bool flag : flags) {
    NS_LOG_INFO("  [" << idx << "] " << flag);
    if (flag) {
      trueCount++;
    }
    idx++;
  }

  NS_LOG_INFO("True count        : " << trueCount);

  if (flags.size() > 5 && trueCount >= 5) {
    NS_LOG_INFO("Shared Bottleneck : ENTERED");
  } else {
    NS_LOG_INFO("Shared Bottleneck : NOT ENTERED");
  }

  NS_LOG_INFO("========================================");
}

// 多种SBD算法
void MultiTcpAvStreamClient::SharedBottleneckDetected() {
  NS_LOG_FUNCTION(this);
  NS_LOG_INFO("time: " << Simulator::Now().GetMicroSeconds() / (double)1000000);
  if (m_enableMptcpSbd) {
    // 进行 MPTCP-SBD 算法
    NS_LOG_INFO("进行 MPTCP-SBD 算法检测共享瓶颈");
    std::pair<bool, bool> inSbd =
        MPTCP_SBD::isSharedBottleneck(m_videoStream.m_sbd, m_audioStream.m_sbd);
    bool mptcp_sbd_result = inSbd.first;
    bool ours_sbd_result = false;
    // 至少有一个流持续走低，增加进入共享瓶颈的可能性
    if (m_videoStream.m_bufferTrendState.isDeclining ||
        m_audioStream.m_bufferTrendState.isDeclining ||
        m_videoStream.m_bufferTrendState.isLow ||
        m_audioStream.m_bufferTrendState.isLow ||
        m_videoStream.m_bufferTrendState.isSevereLow ||
        m_audioStream.m_bufferTrendState.isSevereLow) {
      NS_LOG_INFO("至少有一个流持续走低，增加进入共享瓶颈的可能性");
      ours_sbd_result = inSbd.second;
      NS_LOG_INFO("我们的SBD算法检测结果: "
                  << (ours_sbd_result ? "共享瓶颈" : "非共享瓶颈"));
    }
    m_OurinSbdFlags.push_back(ours_sbd_result);
    if (m_OurinSbdFlags.size() > 10) {
      m_OurinSbdFlags.pop_front();
    }
    int oursCount = 0;
    for (bool flag : m_OurinSbdFlags) {
      if (flag) oursCount++;
    }
    if (ours_sbd_result) {
      NS_LOG_INFO("我们的SBD算法检测到共享瓶颈");
      if (preTimeLog_oursbd != (int)Simulator::Now().GetSeconds()) {
        preTimeLog_oursbd = (int)Simulator::Now().GetSeconds();
        m_OurSbdLog << (int)Simulator::Now().GetSeconds() << "    "
                    << "1" << "\n";
        m_OurSbdLog.flush();
      }
      m_videoStream.m_bufferData.isInSB = true;
      m_audioStream.m_bufferData.isInSB = true;
    } else if (oursCount <= 0) {
      // 防止 启发式的ABR算法频繁的震荡
      m_videoStream.m_bufferData.isInSB = false;
      m_audioStream.m_bufferData.isInSB = false;
    }
    m_MpTcpinSbdFlags.push_back(mptcp_sbd_result);
    NS_LOG_INFO("video PB:" << m_videoStream.m_sbd.GetPB()
                            << "   audio PB:" << m_audioStream.m_sbd.GetPB());

    if (m_MpTcpinSbdFlags.size() > 10) {
      m_MpTcpinSbdFlags.pop_front();
    }
    int count = 0;
    if (m_MpTcpinSbdFlags.size() > 2) {
      for (bool flag : m_MpTcpinSbdFlags) {
        if (flag) count++;
      }
      if (count >= 2) {
        if (preTimeLog_mptcpsbd != (int)Simulator::Now().GetSeconds()) {
          preTimeLog_mptcpsbd = (int)Simulator::Now().GetSeconds();
          m_mptcpsbdLog << preTimeLog_mptcpsbd << "    "
                        << "1" << "\n";
          m_mptcpsbdLog.flush();
          NS_LOG_INFO("MPTCP-SBD进入共享瓶颈状态");
        }
      }
    }
    // DumpSbdFlags(m_MpTcpinSbdFlags, mptcp_sbd_result);
  }
  Simulator::Schedule(Seconds(0.350),
                      &MultiTcpAvStreamClient::SharedBottleneckDetected, this);
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
  m_videoStream.owdLog.close();

  m_audioStream.downloadLog.close();
  m_audioStream.playbackLog.close();
  m_audioStream.adaptationLog.close();
  m_audioStream.bufferLog.close();
  m_audioStream.throughputLog.close();
  m_audioStream.bufferUnderrunLog.close();
  m_audioStream.owdLog.close();

  m_mptcpsbdLog.close();
  m_SbCcLog.close();
  m_OurSbdLog.close();
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
  // Controller(event, type);
  Controller_AV(event, type);
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

// 记录时间窗口相关数据
void MultiTcpAvStreamClient::LogOwdWindow(StreamData& stream,
                                          const OwdInfo& w) {
  NS_LOG_FUNCTION(this << ToStringStreamType(stream.m_type));
  int64_t end_time = w.m_NowWindowEnd;
  int64_t start_time = end_time - WINDOW_MS;
  size_t numbers = w.owds.size();

  int64_t min_owd = 0;
  int64_t max_owd = 0;
  double mean_owd = 0.0;

  if (!w.owds.empty()) {
    min_owd = w.owds[0];
    max_owd = w.owds[0];
    int64_t sum = 0;

    for (auto owd : w.owds) {
      sum += owd;
      if (owd < min_owd) min_owd = owd;
      if (owd > max_owd) max_owd = owd;
    }
    mean_owd = static_cast<double>(sum) / w.owds.size();
  }

  // 写日志（你可以提前在 StreamData 里定义 owdLog）
  stream.owdLog << std::setfill(' ') << std::setw(12)
                << start_time / (double)1000 << " " << std::setfill(' ')
                << std::setw(12) << end_time / (double)1000 << " "
                << std::setfill(' ') << std::setw(8) << numbers << " "
                << std::setfill(' ') << std::setw(8) << min_owd << " "
                << std::setfill(' ') << std::setw(8) << max_owd << " "
                << std::setfill(' ') << std::setw(10) << mean_owd << "\n";

  stream.owdLog.flush();
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
// 记录缓冲区日志
void MultiTcpAvStreamClient::LogBuffer_v2(StreamType streamType,
                                          int64_t timeNow) {
  NS_LOG_FUNCTION(this << ToStringStreamType(streamType));

  StreamData* streamData = GetStreamData(streamType);
  if (streamData == NULL) return;

  // 写入缓冲区日志
  streamData->bufferLog << std::setfill(' ') << std::setw(13) << timeNow / 1e6
                        << " " << std::setfill(' ') << std::setw(13)
                        << (streamData->m_bufferData.m_segmentsInBuffer *
                            streamData->m_bufferData.m_segmentDuration) /
                               1e6
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

  std::string sharedBottleneckLog = basePrefix + "/MptcpSbd.txt";
  m_mptcpsbdLog.open(sharedBottleneckLog.c_str());
  m_mptcpsbdLog << "Time    "
                << "isSharedBottleneck\n";
  m_mptcpsbdLog.flush();

  std::string sbCcLog = basePrefix + "/SbCcLog.txt";
  m_SbCcLog.open(sbCcLog.c_str());
  m_SbCcLog << "Time    "
            << "isSharedBottleneck\n";
  m_SbCcLog.flush();

  std::string ourSbdLog = basePrefix + "/ourSbdLog.txt";
  m_OurSbdLog.open(ourSbdLog.c_str());
  m_OurSbdLog << "Time    "
              << "isSharedBottleneck\n";
  m_OurSbdLog.flush();

  std::string videoPrefix =
      basePrefix + "/" + "video_" + m_videoStream.m_algoName;

  // 初始化音频流日志文件
  std::string audioPrefix =
      basePrefix + "/" + "audio_" + m_audioStream.m_algoName;

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

  // 视频流 OWD 窗口日志
  std::string vOwdLog = videoPrefix + "/owdWindowLog.txt";
  m_videoStream.owdLog.open(vOwdLog.c_str());
  m_videoStream.owdLog << std::setfill(' ') << std::setw(12) << "WinStart"
                       << " " << std::setw(12) << "WinEnd"
                       << " " << std::setw(8) << "Samples_Number"
                       << " " << std::setw(8) << "Min"
                       << " " << std::setw(8) << "Max"
                       << " " << std::setw(10) << "Mean"
                       << "\n";
  m_videoStream.owdLog.flush();

  // 视频流 队列长度日志
  std::string vqLog = videoPrefix + "/queueLog.txt";
  m_videoStream.queueLog.open(vqLog.c_str());
  m_videoStream.queueLog << std::setfill(' ') << std::setw(12) << "Time_Now"
                         << " " << std::setw(12) << "EnQueue"
                         << " " << std::setw(8) << "Queue"
                         << " " << std::setw(8) << "Dequeue"
                         << " " << std::setw(8) << "Drop"
                         << "\n";
  m_videoStream.queueLog.flush();

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

  // 音频流 OWD 窗口日志
  std::string aOwdLog = audioPrefix + "/owdWindowLog.txt";
  m_audioStream.owdLog.open(aOwdLog.c_str());
  m_audioStream.owdLog << std::setfill(' ') << std::setw(12) << "WinStart"
                       << " " << std::setw(12) << "WinEnd"
                       << " " << std::setw(8) << "Samples_Number"
                       << " " << std::setw(8) << "Min"
                       << " " << std::setw(8) << "Max"
                       << " " << std::setw(10) << "Mean"
                       << "\n";
  m_audioStream.owdLog.flush();

  // 音频流 队列长度日志
  std::string aqLog = audioPrefix + "/queueLog.txt";
  m_audioStream.queueLog.open(aqLog.c_str());
  m_audioStream.queueLog << std::setfill(' ') << std::setw(12) << "Time_Now"
                         << " " << std::setw(12) << "EnQueue"
                         << " " << std::setw(8) << "Queue"
                         << " " << std::setw(8) << "Dequeue"
                         << " " << std::setw(8) << "Drop"
                         << "\n";
  m_audioStream.queueLog.flush();

  NS_LOG_INFO("Log files initialized for client " << clientId);
}

}  // namespace ns3