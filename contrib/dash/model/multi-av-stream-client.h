#ifndef MULTI_TCP_AV_STREAM_CLIENT_H
#define MULTI_TCP_AV_STREAM_CLIENT_H

#include <fstream>
#include <iostream>
#include <map>

#include "festive.h"
#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ipv4-address.h"
#include "ns3/ptr.h"
#include "ns3/traced-callback.h"
#include "panda.h"
#include "tcp-stream-adaptation-algorithm.h"
#include "tcp-stream-interface.h"
#include "tobasco2.h"

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
  void Initialise(std::string algorithm, uint16_t clientId);

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
    bool m_bufferUnderrun;           //!< 是否发生缓冲区下溢
    int64_t m_currentPlaybackIndex;  //!< 当前播放段索引
    int64_t m_segmentsInBuffer;      //!< 缓冲区内段数
    int64_t m_currentRepIndex;       //!< 当前请求段质量索引
    int64_t m_lastSegmentIndex;      //!< 最后一个段索引，总段数-1
    int64_t m_segmentCounter;        //!< 下一个下载段索引

    int64_t m_transmissionStartReceivingSegment;  //!< 段传输开始时间（微秒）
    int64_t m_transmissionEndReceivingSegment;    //!< 段传输结束时间（微秒）
    int64_t m_bytesReceived;                      //!< 当前包已接收字节数
    int64_t m_bDelay;           //!< 播放时最小缓冲区水平，下一下载开始前
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

    int64_t m_downloadRequestSent;  //!< 下载请求发送时间

    // 吞吐量和缓冲区数据
    throughputData m_throughput;  //!< 吞吐量跟踪数据
    bufferData m_bufferData;      //!< 缓冲区跟踪数据
    playbackData m_playbackData;  //!< 播放跟踪数据

    videoData m_segmentData;  //!< 段信息

    controllerState state;  //!< 当前状态机状态

    bool m_SegmentReceived;  //!< 段是否已接收
  };

  virtual void StartApplication(void);
  virtual void StopApplication(void);

  // 主控制器状态机
  void Controller(controllerEvent event, StreamType type);

  void PlaybackController(controllerEvent event);

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

  /**
   * \brief AV 同步播放：当 A/V都启用时，
   * 统一调用此函数进行同时播放并记录时间上面的播放差异。
   * \return false 表示成功播放（双方都播放了一段），true 表示未播放。
   */
  bool PlaybackHandleAV();

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
};

}  // namespace ns3

#endif /* MULTI_TCP_AV_STREAM_CLIENT_H */