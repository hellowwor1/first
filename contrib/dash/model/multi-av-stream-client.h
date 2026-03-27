#ifndef MULTI_TCP_AV_STREAM_CLIENT_H
#define MULTI_TCP_AV_STREAM_CLIENT_H

#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <vector>

#include "OwdManager.h"
#include "abr.h"
#include "audio-festive.h"
#include "bba.h"
#include "festive.h"
#include "ns3/application.h"
#include "ns3/core-module.h"
#include "ns3/event-id.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-address.h"
#include "ns3/network-module.h"
#include "ns3/ptr.h"
#include "ns3/sb-cc.h"
#include "ns3/tcp-socket-base.h"
#include "ns3/traced-callback.h"
#include "panda.h"
#include "rtt.h"
#include "sabba.h"
#include "tcp-stream-adaptation-algorithm.h"
#include "tcp-stream-interface.h"
#include "tobasco2.h"
// 打入流标签
#include "flowtag.h"
// 共享瓶颈检测头文件
#include "mptcp-sbd.h"
#include "shared-bottleneck-detection.h"
namespace ns3 {

class Socket;
class Packet;
/**
 * \ingroup multiTcpAvStream
 * \brief 多TCP流AV客户端
 *
 * 支持同时连接视频服务器和音频服务器，分别获取视频流和音频流。
 * 每个流使用独立的TCP连接和自适应算法。
 */
class MultiTcpAvStreamClient : public Application {
 public:
  /**
   * \brief
   * 在ip层回调，当有新的数据包带来时，更新Owd
   */
  void NotifyOwd(bool isVideo, int64_t rxTime, int64_t owd);

  /**
   * \brief
   * 在ip层回调，当有带有ECN标志的数据包到达客户端的ip层时，调用该函数记录到m_ecnEvents中
   */
  void NotifyEcn(Ipv4Address srcIp, uint16_t srcPort, Time rxTime);
  /**
   * \brief 获取对象的 TypeId。
   * \return 对象的 TypeId
   */
  static TypeId GetTypeId(void);
  MultiTcpAvStreamClient();
  virtual ~MultiTcpAvStreamClient();

  /**
   * \brief 初始化客户端实例
   *
   * 创建视频和音频流的自适应算法对象，读取相应的段大小文件。
   *
   * \param algorithm 使用的自适应算法名称
   * \param clientId 客户端ID
   */
  void Initialise(std::string video_algorithm, std::string audio_algorithm,
                  uint16_t clientId);
  /**
   * \brief 设置视频服务器的远程地址和端口
   * \param ip 视频服务器的IPv4地址
   * \param port 视频服务器的端口
   */
  void SetVideoRemote(Ipv4Address ip, uint16_t port);

  /**
   * \brief 设置音频服务器的远程地址和端口
   * \param ip 音频服务器的IPv4地址
   * \param port 音频服务器的端口
   */
  void SetAudioRemote(Ipv4Address ip, uint16_t port);

  /**
   * \brief 设置视频服务器的远程地址和端口（通用地址类型）
   * \param ip 视频服务器的地址IPv6
   * \param port 视频服务器的端口
   */
  void SetVideoRemote(Ipv6Address ip, uint16_t port);

  /**
   * \brief 设置音频服务器的远程地址和端口（通用地址类型）
   * \param ip 音频服务器的地址IPv6
   * \param port 音频服务器的端口
   */
  void SetAudioRemote(Ipv6Address ip, uint16_t port);
  /**
   * \brief 设置远程地址和端口
   * \param ip 远程 IP 地址
   * \param port 远程端口
   */
  void SetVideoRemote(Address ip, uint16_t port);

  /**
   * \brief 设置远程地址和端口
   * \param ip 远程 IP 地址
   * \param port 远程端口
   */
  void SetAudioRemote(Address ip, uint16_t port);

 protected:
  virtual void DoDispose(void);

 private:
  /**
   * \brief 定义客户端状态机的状态
   *
   * 与单流客户端类似，但需要同时管理视频和音频流
   */
  enum controllerState {
    initial,             //!< 初始状态
    downloading,         //!< 只下载状态
    downloadingPlaying,  //!< 下载+播放状态
    playing,             //!< 只播放状态
    terminal             //!< 终止状态
  };
  // controllerState state;  //!< 当前状态机状态

  /**
   * \brief 定义客户端状态机的事件
   */
  enum controllerEvent {
    downloadFinished,  //!< 下载完成事件
    playbackFinished,  //!< 播放完成事件
    irdFinished,       //!< 初始下载延迟完成事件
    init               //!< 初始化事件
  };

  /**
   * \brief 流类型枚举
   */
  enum StreamType {
    VIDEO_STREAM,  //!< 视频流
    AUDIO_STREAM   //!< 音频流
  };

  /**
   * \brief 客户端需要请求哪些数据
   */
  enum StreamSelection {
    VIDEO_ONLY,  //!< 只请求视频
    AUDIO_ONLY,  //!< 只请求音频
    AUDIO_VIDEO  //!< 请求音视频
  };

  /**
   * \brief 管理记录ECN标志
   */
  struct EcnEvent {
    Time rxTime;        // 客户端收到 CE 的时间
    Ipv4Address srcIp;  // 源 IP（音频/视频服务器）
    uint16_t srcPort;   // 源端口（区分音频/视频流）
  };

  /**
   * \brief 管理每个流数据的结构体
   */
  struct StreamData {
    uint32_t m_dataSize;  //!< 包负载大小
    uint8_t* m_data;      //!< 包负载数据

    Ptr<Socket> m_socket;   //!< 流的套接字
    Address m_peerAddress;  //!< 远程服务器地址
    uint16_t m_peerPort;    //!< 远程服务器端口
    StreamType m_type;      //!< 流类型枚举

    std::string
        m_segmentSizeFilePath;  //!< 包含段大小文件的路径（相对于 ns-3.x 目录）

    AdaptationAlgorithm* algo;  //!< 流使用的自适应算法
    std::string m_algoName;     //!< 流使用的算法名称
    // 播放相关状态
    bool m_bufferUnderrun;              //!< 是否发生缓冲区下溢
    int64_t m_bufferUnderrunStartTime;  //!< 缓冲区下溢开始时间（微秒）

    int64_t m_currentPlaybackIndex;  //!< 当前播放段索引
    int64_t m_segmentsInBuffer;      //!< 缓冲区内段数
    int64_t m_currentRepIndex;       //!< 当前请求段质量索引
    int64_t m_lastSegmentIndex;      //!< 最后一个段索引，总段数-1
    int64_t m_segmentCounter;        //!< 下一个下载段索引

    int64_t m_transmissionStartReceivingSegment;  //!< 段传输开始时间（微秒）
    int64_t m_transmissionEndReceivingSegment;    //!< 段传输结束时间（微秒）
    int64_t m_bytesReceived;                      //!< 当前包已接收字节数
    int64_t m_bDelay;
    int64_t m_highestRepIndex;  //!< 最高表示级别索引
    uint64_t m_segmentDuration;

    // 日志文件
    // 暂时缺少一个自适应的码率日志输出流
    std::ofstream downloadLog;        //!< 下载日志文件流
    std::ofstream playbackLog;        //!< 播放日志文件流
    std::ofstream adaptationLog;      //!< 自适应算法日志文件流
    std::ofstream bufferLog;          //!< 缓冲区日志文件流
    std::ofstream throughputLog;      //!< 吞吐量日志文件流
    std::ofstream bufferUnderrunLog;  //!< 缓冲区下溢日志输出流
    std::ofstream owdLog;             //!< OWD 窗口日志输出流
    std::ofstream queueLog;           //!< 队列长度日志输出流

    int64_t m_downloadRequestSent;  //!< 下载请求发送时间

    // 吞吐量和缓冲区数据
    throughputData m_throughput = {};     //!< 吞吐量跟踪数据
    bufferData m_bufferData = {};         //!< 缓冲区跟踪数据
    BufferTrendState m_bufferTrendState;  //!< 缓冲区趋势状态
    playbackData m_playbackData = {};     //!< 播放跟踪数据

    videoData m_segmentData = {};  //!< 段信息

    controllerState state;  //!< 当前状态机状态

    bool m_SegmentReceived;  //!< 段是否已接收

    std::queue<int64_t> m_ptsQueue;  // 每段 segment 的 PTS（显示时间）

    // 每个流都记录自己的Rtt信息，用以检测共享瓶颈
    std::deque<RttEvent> m_Rtts;
    Time m_LatestRtt;

    // 每个流记录OWD信息，用以实现MPTCP-SBD算法
    /*
      存储格式：pair<每个时间窗口的OWD样本列表, 时间窗口结束时间>
      以deque形式存储，方便删除过期样本
    */
    MPTCP_SBD m_sbd;  // 共享瓶颈检测对象

    double* m_qoe;  // 引用外部QoE变量，直接更新QoE得分
  };

  virtual void StartApplication(void);
  virtual void StopApplication(void);

  // 主控制器状态机
  void Controller(controllerEvent event, StreamType type);
  std::string ToStringControllerEvent(controllerEvent event);
  std::string ToStringStreamType(StreamType type);
  std::string ToStringControllerState(controllerState state);

  void Controller_AV(controllerEvent event, StreamType type);

  /**
   * 设置包数据内容，将 T & message 字符串的以零结尾内容填充到 m_data 中
   * \brief 准备发送的数据包
   * \param message 要发送的消息（字节数）
   */
  template <typename T>
  void PreparePacket(T& message);

  /**
   * \brief 向指定流发送数据包
   * \param message 要发送的消息（字节数）
   * \param streamType 流类型
   * 发送前会调用 PreparePacket(T & message) 填充数据，包含请求的字节数。
   */
  template <typename T>
  void Send(T& message, StreamData* streamData);

  /**
   * \brief 处理数据包接收
   * \param socket 接收到数据的套接字
   */
  void HandleRead(Ptr<Socket> socket);

  // 维护OWD的滑动窗口数据
  void AddOwdSample(StreamData& stream, int64_t timeNow, int64_t owd);
  // 打印OWD窗口数据（调试用）
  void DumpOwdWindows(const StreamData& stream, int64_t timeNow) const;

  /**
   * \brief 获取套接字对应的流类型
   * \param socket 套接字
   * \return 流类型枚举
   */
  StreamType GetStreamTypeFromSocket(Ptr<Socket> socket);

  /**
   * \brief 获取流数据指针
   * \param streamType 流类型
   * \return 流数据指针
   */
  StreamData* GetStreamData(StreamType streamType);

  StreamData* GetOtherStreamData(StreamType streamType);

  /**
   * \brief 连接成功回调
   * \param socket 连接成功的套接字
   */
  void ConnectionSucceeded(Ptr<Socket> socket);

  /**
   * \brief 连接成功调用控制器
   * \param type 流类型
   */
  void StartStreamController(StreamType type);

  /**
   * \brief 连接失败回调
   * \param socket 连接失败的套接字
   */
  void ConnectionFailed(Ptr<Socket> socket);

  /**
   * \brief 处理段接收完成
   * \param streamType 完成接收的流类型
   * 当段完整接收后调用，即接收的字节数等于请求的字节数。记录吞吐量和缓冲区数据。
   */
  void SegmentReceivedHandle(StreamType streamType);

  /**
   * \brief 为指定流请求下一个码率索引
   * \param streamdata 流
   */
  void RequestRepIndex(StreamData* streamData);

  /**
   * \brief 读取视频段大小文件
   * \param segmentSizeFile 文件路径
   * \param isVideo 是否为视频文件
   * \return 成功返回1，失败返回-1
   */
  int ReadInBitrateValues(std::string segmentSizeFile, bool isVideo);

  bool PlaybackHandleAV(StreamData& stream);

  /**
   * \brief
   * 判断音频、视频是否可以一起播放，换言之就是音频、视频缓冲区是否同时都有时间可以播放
   * \return false 不可以一起播放(都没有数据/只有一方有数据)，
   *  true 表示可以一起播放(音频、视频缓冲区都有数据)。
   */
  bool CanPlayAvTogether();

  /**
   * \brief 对单个流控制/模拟播放过程
   * \param stream 流数据
   * \return false 表示成功播放了一个 segment，true 表示没有播放（buffer
   *          underrun / 等待）
   */
  bool PlaybackHandleSingle(StreamData& stream);

  /**
   * \brief 判断流的缓冲区是否耗尽
   * \param stream 流数据类型
   * \return false 表示没有耗尽，true 表示耗尽了
   */
  bool IsBufferEmpty(StreamType type);

  /**
   * \brief 记录指定流的下载信息
   * \param streamType 流类型
   */
  void LogDownload(StreamType streamType);

  /**
   * \brief 记录指定流的缓冲区信息
   * \param streamType 流类型
   */
  void LogBuffer(StreamType streamType);

  /**
   * \brief 原先的buffer日志记录函数有问题，现在更改之后的逻辑为
   * 1.段接收完成，缓冲区增加时记录
   * 2.播放的时候，缓冲区减少时记录
   * \param streamType 流类型
   * \param timeNow 当前记录的日志时间
   */
  void LogBuffer_v2(StreamType streamType, int64_t timeNow);
  /**
   * \brief 记录指定流的吞吐量信息
   * \param packetSize 数据包大小
   * \param streamType 流类型
   */
  void LogThroughput(uint32_t packetSize, StreamType streamType);

  /**
   * \brief 记录指定流的播放信息
   * \param streamType 流类型
   */
  void LogPlayback(StreamType streamType);

  /**
   * \brief 记录指定流的自适应算法信息
   * \param answer 算法返回结果
   * \param streamType 流类型
   */
  void LogAdaptation(algorithmReply answer, StreamData* streamData);

  /**
   * \brief 初始化所有日志文件
   * \param simulationId 仿真ID
   * \param clientId 客户端ID
   * \param numberOfClients 客户端总数
   */
  void InitializeLogFiles(std::string simulationId, std::string clientId,
                          std::string numberOfClients);

  /**
   * \brief 检查两个流是否都已连接
   * \return true 表示两个流都已连接
   */
  bool BothStreamsConnected();

  /**
   * \brief 检查两个流的当前段是否都已接收完成
   * \return true 表示两个流的当前段都已接收完成
   */
  bool BothSegmentsReceived();

  // 音视频的Rtt Trace
  void VideoRttTrace(Time oldRtt, Time newRtt);
  void AudioRttTrace(Time oldRtt, Time newRtt);
  // Rtt 采样
  void SampleOneStreamRtt(StreamData& stream);
  void SampleVideoRtt();
  void SampleAudioRtt();

  // (旧版)定时共享瓶颈检测
  void DetectSbd();

  // 多种共享瓶颈检测算法
  void SharedBottleneckDetected();
  // 状态打印函数
  void DumpSbdFlags(const std::deque<bool>& flags, bool currentInSbd) const;
  void LogOwdWindow(StreamData& stream, const OwdInfo& w);

  void VideoCwndChange(uint32_t oldCwnd, uint32_t newCwnd);
  void AudioCwndChange(uint32_t oldCwnd, uint32_t newCwnd);

  void VideoBytesInFlightTrace(uint32_t oldBytes, uint32_t newBytes);
  void AudioBytesInFlightTrace(uint32_t oldBytes, uint32_t newBytes);

  void VideoRtxTrace(SequenceNumber32 seq);
  void AudioRtxTrace(SequenceNumber32 seq);

  void VideoOwdTrace(Time owd, SequenceNumber32 txSeq, uint32_t payloadSize,
                     bool isRetrans);
  void AudioOwdTrace(Time owd, SequenceNumber32 txSeq, uint32_t payloadSize,
                     bool isRetrans);

  // 每当缓冲区发生变化的时候，就进行更新，以便后续做共享瓶颈检测
  void UpdateBufferState(int64_t nowMs, int64_t bufferMs, BufferTrendState& s);
  // 客户端发送的数据包
  uint32_t m_dataSize;  //!< 数据包负载大小
  uint8_t* m_data;      //!< 数据包负载数据
  // 视频流数据
  StreamData m_videoStream;  //!< 视频流数据

  // 音频流数据
  StreamData m_audioStream;  //!< 音频流数据

  // 视频服务器地址/端口
  Address GetVideoRemoteAddress() const { return m_videoStream.m_peerAddress; }
  void SetVideoRemoteAddress(Address a) { m_videoStream.m_peerAddress = a; }

  uint16_t GetVideoRemotePort() const { return m_videoStream.m_peerPort; }
  void SetVideoRemotePort(uint16_t p) { m_videoStream.m_peerPort = p; }

  // 音频服务器地址/端口
  Address GetAudioRemoteAddress() const { return m_audioStream.m_peerAddress; }
  void SetAudioRemoteAddress(Address a) { m_audioStream.m_peerAddress = a; }

  uint16_t GetAudioRemotePort() const { return m_audioStream.m_peerPort; }
  void SetAudioRemotePort(uint16_t p) { m_audioStream.m_peerPort = p; }

  uint16_t m_clientId;                //!< 客户端ID
  uint16_t m_simulationId;            //!< 仿真ID
  uint16_t m_numberOfClients;         //!< 客户端总数
  StreamSelection m_streamSelection;  //!< 请求数据类型

  std::string m_algoName;  //!< 使用的自适应算法名称

  // 流连接状态
  bool m_videoConnected;  //!< 视频流是否已连接
  bool m_audioConnected;  //!< 音频流是否已连接

  // 流接收状态
  bool m_videoSegmentReceived;  //!< 视频段是否已接收
  bool m_audioSegmentReceived;  //!< 音频段是否已接收

  // 文件路径
  std::string m_videoSegmentSizeFilePath;  //!< 视频段大小文件路径
  std::string m_audioSegmentSizeFilePath;  //!< 音频段大小文件路径

  int64_t m_bDelay;            //!< 缓冲区数据不够，网络太差时，暂停请求的时间
  uint64_t m_segmentDuration;  //!< 段持续时间（微秒）

  std::ofstream m_avSyncLog;             //!< 记录同步/不同步的情况
  const int64_t m_syncWindowUs = 50000;  // 50 ms 的宽松窗口（微秒）

  // Pts 播放时间戳，用以音视频同步播放
  int64_t videoPts;
  int64_t audioPts;

  // Rtt采样率，采样周期
  Time m_RttSampleInterval;
  // 采样的能够容纳的数量大小 ，最后持续时间=采样周期*采样的最大个数
  size_t m_maxRttSamples;
  // 封装好的共享瓶颈检测对象
  SharedBottleneckDetection m_sbd;

  // 开启流互相感知的ABR算法操作
  bool m_enableSharedBottleneckAwareAbr;

  // 开启 MPTCP-SBD（INFOCOM 2016） 算法支持
  bool m_enableMptcpSbd;
  static constexpr int64_t WINDOW_MS = 350;
  //   static constexpr size_t MAX_WINDOWS = 50;
  static constexpr size_t MAX_WINDOWS = 15;
  static constexpr int64_t MAX_RANGE_MS = WINDOW_MS * MAX_WINDOWS;  // 17.5s
  std::deque<bool> m_MpTcpinSbdFlags;  // 每个时间窗口是否处于共享瓶颈的标志

  std::ofstream m_mptcpsbdLog;  //!< 共享瓶颈 日志输出流

  // 缓冲区变化趋势的判断阈值
  const int64_t LOW_TH_MS = 4000;         // 4s
  const int64_t SEVERE_LOW_TH_MS = 2000;  // 2s
  const int64_t PERSIST_TH_MS = 3000;     // 3s
  const int64_t EPS_MS = 200;             // 200ms 抖动过滤

  // CE 标志数组
  std::vector<EcnEvent> m_ecnEvents;
  // SB-CC 算法对象
  SbCc m_sbCc;
  std::ofstream m_SbCcLog;  //!< sb-cc算法的共享瓶颈 日志输出流
  int preTimeLog_sbcc = -1;

  std::ofstream m_OurSbdLog;  //!< Our算法的共享瓶颈 日志输出流
  int preTimeLog_oursbd = -1;
  std::deque<bool> m_OurinSbdFlags;  // 每个时间窗口是否处于共享瓶颈的标志

  int preTimeLog_mptcpsbd = -1;

  // QoE 相关系数
  const int qoe_alpha = 4;  // 重缓冲时间权重
  const int qoe_beta = 1;   // 质量不平衡扣分权重
  std::vector<double> qoe_R =
      std::vector<double>(400, 0.0);  // Ri表示第i个块的音视频比特率总和
  double qoe_Rs;                      // [1,i]的总音视频比特率总和
  std::vector<double> qoe_T =
      std::vector<double>(400, 0.0);  // Ti表示传输第i个块期间的rebuffer时间
  double qoe_Ts;                      // [1,i]的总rebuffer时间
  std::vector<double> qoe_D =
      std::vector<double>(400, 0.0);  // Di表示第i个块的质量不平衡程度
  double qoe_Ds;                      // [1,i]的总质量不平衡程度
  double
      qoe;  // 综合QoE得分 qoe = qoe_Rs - qoe_alpha * qoe_Ts - qoe_beta * qoe_Ds
};

}  // namespace ns3

#endif /* MULTI_TCP_AV_STREAM_CLIENT_H */