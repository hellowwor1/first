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
   * \param ip 视频服务器的地址
   * \param port 视频服务器的端口
   */
  void SetVideoRemote(Address ip, uint16_t port);

  /**
   * \brief 设置音频服务器的远程地址和端口（通用地址类型）
   * \param ip 音频服务器的地址
   * \param port 音频服务器的端口
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

  controllerState state;  //!< 当前状态机状态

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
   * \brief 管理每个流数据的结构体
   */
  struct StreamData {
    AdaptationAlgorithm* algo;  //!< 流使用的自适应算法
    Ptr<Socket> socket;         //!< 流的套接字
    Address peerAddress;        //!< 远程服务器地址
    uint16_t peerPort;          //!< 远程服务器端口
    std::string streamType;     //!< 流类型字符串："video" 或 "audio"
    StreamType type;            //!< 流类型枚举

    // 视频数据相关
    videoData videoInfo;                        //!< 视频段信息
    int64_t currentRepIndex;                    //!< 当前码率索引
    int64_t segmentCounter;                     //!< 已下载段计数器
    int64_t bytesReceived;                      //!< 当前段已接收字节数
    int64_t transmissionStartReceivingSegment;  //!< 当前段接收开始时间
    int64_t transmissionEndReceivingSegment;    //!< 当前段接收结束时间
    int64_t downloadRequestSent;                //!< 下载请求发送时间

    // 吞吐量和缓冲区数据
    throughputData throughput;  //!< 吞吐量跟踪数据
    bufferData bufferData;      //!< 缓冲区跟踪数据
    playbackData playbackData;  //!< 播放跟踪数据

    // 统计信息
    int64_t lastSegmentIndex;  //!< 最后一个段索引
    int64_t highestRepIndex;   //!< 最高码率索引
    uint64_t segmentDuration;  //!< 段持续时间（微秒）

    // 日志文件
    std::ofstream downloadLog;    //!< 下载日志文件流
    std::ofstream playbackLog;    //!< 播放日志文件流
    std::ofstream adaptationLog;  //!< 自适应算法日志文件流
    std::ofstream bufferLog;      //!< 缓冲区日志文件流
    std::ofstream throughputLog;  //!< 吞吐量日志文件流
  };

  virtual void StartApplication(void);
  virtual void StopApplication(void);

  // 主控制器状态机
  void Controller(controllerEvent action);

  /**
   * \brief 准备发送的数据包
   * \param message 要发送的消息（字节数）
   */
  template <typename T>
  void PreparePacket(T& message);

  /**
   * \brief 向指定流发送数据包
   * \param message 要发送的消息（字节数）
   * \param streamType 流类型
   */
  template <typename T>
  void Send(T& message, StreamType streamType);

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
   * \brief 连接失败回调
   * \param socket 连接失败的套接字
   */
  void ConnectionFailed(Ptr<Socket> socket);

  /**
   * \brief 处理段接收完成
   * \param streamType 完成接收的流类型
   */
  void SegmentReceivedHandle(StreamType streamType);

  /**
   * \brief 为指定流请求下一个码率索引
   * \param streamType 流类型
   */
  void RequestRepIndex(StreamType streamType);

  /**
   * \brief 读取视频段大小文件
   * \param segmentSizeFile 文件路径
   * \param isVideo 是否为视频文件
   * \return 成功返回1，失败返回-1
   */
  int ReadInBitrateValues(std::string segmentSizeFile, bool isVideo);

  /**
   * \brief 控制/模拟播放过程
   * \return true 表示发生缓冲区下溢
   */
  bool PlaybackHandle();

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
  void LogAdaptation(algorithmReply answer, StreamType streamType);

  /**
   * \brief 初始化所有日志文件
   * \param simulationId 仿真ID
   * \param clientId 客户端ID
   * \param numberOfClients 客户端总数
   */
  void
  InitializeLogFiles(std::string simulationId, std::string clientId, std::string numberOfClients);

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

  uint32_t m_dataSize;  //!< 数据包负载大小
  uint8_t* m_data;      //!< 数据包负载数据

  // 视频流数据
  StreamData m_videoStream;  //!< 视频流数据

  // 音频流数据
  StreamData m_audioStream;  //!< 音频流数据

  uint16_t m_clientId;         //!< 客户端ID
  uint16_t m_simulationId;     //!< 仿真ID
  uint16_t m_numberOfClients;  //!< 客户端总数

  std::string m_algoName;  //!< 使用的自适应算法名称

  // 播放相关状态
  bool m_bufferUnderrun;           //!< 是否发生缓冲区下溢
  int64_t m_currentPlaybackIndex;  //!< 当前播放段索引
  int64_t m_segmentsInBuffer;      //!< 缓冲区内段数

  // 流连接状态
  bool m_videoConnected;  //!< 视频流是否已连接
  bool m_audioConnected;  //!< 音频流是否已连接

  // 流接收状态
  bool m_videoSegmentReceived;  //!< 视频段是否已接收
  bool m_audioSegmentReceived;  //!< 音频段是否已接收

  // 文件路径
  std::string m_videoSegmentSizeFilePath;  //!< 视频段大小文件路径
  std::string m_audioSegmentSizeFilePath;  //!< 音频段大小文件路径

  // 缓冲区下溢日志
  std::ofstream bufferUnderrunLog;  //!< 缓冲区下溢日志文件流

  // 播放延迟（取两个流中的最大值）
  int64_t m_bDelay;  //!< 播放时最小缓冲区水平，下一下载开始前
};

}  // namespace ns3

#endif /* MULTI_TCP_AV_STREAM_CLIENT_H */