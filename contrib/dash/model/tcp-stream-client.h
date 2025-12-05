#ifndef TCP_STREAM_CLIENT_H
#define TCP_STREAM_CLIENT_H

#include <fstream>
#include <iostream>

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
 * \ingroup tcpStream
 * \brief 一个 TCP 流客户端
 *
 * 每个段大小请求由服务器返回，并在此接收。
 */
class TcpStreamClient : public Application {
 public:
  /**
   * \brief 获取对象的 TypeId。
   * \return 对象的 TypeId
   */
  static TypeId GetTypeId(void);
  TcpStreamClient();
  virtual ~TcpStreamClient();

  /**
   * \brief 设置客户端实例应使用的自适应算法。
   *
   * 通过调用已有 AdaptationAlgorithm 子类的构造函数创建一个新的自适应算法对象。
   *
   * \param algorithm 用于实例化自适应算法对象的算法名称。
   */
  void Initialise(std::string algorithm, uint16_t clientId);

  /**
   * \brief 设置远程地址和端口
   * \param ip 远程 IPv4 地址
   * \param port 远程端口
   */
  void SetRemote(Ipv4Address ip, uint16_t port);
  /**
   * \brief 设置远程地址和端口
   * \param ip 远程 IPv6 地址
   * \param port 远程端口
   */
  void SetRemote(Ipv6Address ip, uint16_t port);
  /**
   * \brief 设置远程地址和端口
   * \param ip 远程 IP 地址
   * \param port 远程端口
   */
  void SetRemote(Address ip, uint16_t port);

 protected:
  virtual void DoDispose(void);

 private:
  /**
   * \brief 定义客户端状态机的状态。
   */
  enum controllerState {
    initial,
    downloading,
    downloadingPlaying,
    playing,
    terminal
  };
  controllerState state;

  /**
   * \brief 定义客户端状态机的事件。
   */
  enum controllerEvent {
    downloadFinished,
    playbackFinished,
    irdFinished,
    init
  };
  AdaptationAlgorithm *algo;

  virtual void StartApplication(void);
  virtual void StopApplication(void);

  /**
   * \brief 控制客户端的有限状态机。
   *
   * 当客户端对象创建时，会执行以下初始化并进入 initial 状态：
   * - 创建指定类型的自适应算法对象。
   * - 从指定的 MPD 文件中读取段信息，包括每个段的持续时间（微秒）和 n×m 矩阵（n
   * 个码率表示级别，m 个段大小，单位字节）。
   * - 初始化日志文件。
   *
   * 初始化完成后，客户端会建立 TCP 连接，并设置连接成功和接收回调。
   * 然后控制器通过调用 RequestRepIndex() 进行状态转换
   * initialinit->downloading，从自适应算法获取下一个下载段的表示级别。
   * 客户端向服务器发送请求，包含需要下载段的字节数，服务器处理请求并开始发送TCP包。
   * 当包到达时触发SetRecvCallback，客户端接收数据，并记录所有到达的数据包。
   * 当接收到的数据量等于请求的段大小时，记录吞吐量并调用控制器事件downloadFinished
   * 控制器将段加入缓冲区，并调用 PlaybackHandle() 模拟播放。
   * 定时器触发下一个 PlaybackHandle()，继续请求下一个段。状态机在 downloading
   * 和 downloadingPlaying 状态之间切换，直到所有段播放完毕。
   */
  void Controller(controllerEvent action);
  /**
   * 设置包数据内容，将 T & message 字符串的以零结尾内容填充到 m_data 中。
   *
   * \param message 客户端请求服务器发送的字节数。
   */
  template <typename T>
  void PreparePacket(T &message);
  /**
   * \brief 向服务器发送一个包。
   *
   * 发送前会调用 PreparePacket(T & message) 填充数据，包含请求的字节数。
   */
  template <typename T>
  void Send(T &message);
  /**
   * \brief 处理包接收。
   *
   * 由下层调用，触发 SetRecvCallback。
   * 当接收字节数等于期望段大小时，调用 SegmentReceivedHandle()。
   *
   * \param socket 接收包的套接字
   */
  void HandleRead(Ptr<Socket> socket);
  /**
   * \brief 当连接成功时触发。
   */
  void ConnectionSucceeded(Ptr<Socket> socket);
  /**
   * \brief 当连接失败时触发。
   */
  void ConnectionFailed(Ptr<Socket> socket);
  /**
   * 当段完整接收后调用，即接收的字节数等于请求的字节数。记录吞吐量和缓冲区数据。
   */
  void SegmentReceivedHandle();
  /**
   * \brief 读取码率值
   *
   * 测试用的码率值以字节为单位，按绝对大小提供（非每秒）。
   * 以 2x2 矩阵形式，每行表示一个码率等级，段大小之间用空格分隔。
   */
  int ReadInBitrateValues(std::string segmentSizeFile);
  /*
   * \brief 控制/模拟播放过程
   *
   * 当段播放完成时，由定时器调用。
   * 如果 m_segmentsInBuffer > 0，则缓冲区减一，m_currentPlaybackIndex 增加。
   * 若之前发生缓冲区下溢，则 m_bufferUnderrun 置为 false 并记录事件。
   * 若缓冲区为空，记录缓冲区下溢事件并设置 m_bufferUnderrun 为 true。
   *
   * \return true 表示发生缓冲区下溢
   */
  bool PlaybackHandle();
  /*
   * \brief 从算法请求下一个表示级别索引。
   *
   * 调用 algo->GetNextRep(int64_t m_segmentCounter) 获取下一个段的表示级别。
   * 返回的结果存储在本地变量，用于日志记录。
   */
  void RequestRepIndex();
  /*
   * \brief 记录段下载信息
   *
   * - 段索引
   * - 发送下载请求时间
   * - 段首包到达时间
   * - 段末包到达时间
   * - 当前段大小（位）
   * - 下载确认 Y/N
   */
  void LogDownload();
  /*
   * \brief 记录缓冲区等级
   *
   * 记录完整下载段到达时间，并将 m_segmentDuration 加到缓冲区等级上。
   *
   * - 段下载完成时间
   * - 段加入前缓冲区等级
   * - 段加入后缓冲区等级
   */
  void LogBuffer();
  /*
   * \brief 记录单个 TCP 包的吞吐量信息
   *
   * - 包到达时间
   * - 包大小
   */
  void LogThroughput(uint32_t packetSize);
  /*
   * \brief 记录播放过程信息
   *
   * - 下一个播放段的索引
   * - 播放段开始时间
   */
  void LogPlayback();
  /*
   * \brief 记录自适应算法信息
   *
   * - 当前段索引
   * - 算法决策时间
   * - 算法决策情况
   * - 是否延迟下一下载
   * \param answer 算法返回的结果
   */
  void LogAdaptation(algorithmReply answer);
  /*
   * \brief 打开日志输出文件
   *
   * 打开 TcpStreamClient 定义的输出流，并创建包含使用算法的日志文件。
   */
  void InitializeLogFiles(std::string simulationId, std::string clientId,
                          std::string numberOfClients);

  uint32_t m_dataSize;  //!< 包负载大小
  uint8_t *m_data;      //!< 包负载数据

  Ptr<Socket> m_socket;   //!< 套接字
  Address m_peerAddress;  //!< 远程地址
  uint16_t m_peerPort;    //!< 远程端口

  uint16_t m_clientId;         //!< 客户端 ID，用于日志记录
  uint16_t m_simulationId;     //!< 仿真 ID，用于日志记录
  uint16_t m_numberOfClients;  //!< 客户端总数
  /*
      添加音频、视频块文件的路径
  */
  std::string
      m_segmentSizeFilePath;  //!< 包含段大小文件的路径（相对于 ns-3.x 目录）
  std::string m_videosegmentSizeFilePath;
  std::string m_audiosegmentSizeFilePath;

  std::string m_algoName;          //!< 客户端使用的自适应算法名称
  bool m_bufferUnderrun;           //!< 是否发生缓冲区下溢
  int64_t m_currentPlaybackIndex;  //!< 当前播放段索引
  int64_t m_segmentsInBuffer;      //!< 缓冲区内段数
  int64_t m_currentRepIndex;       //!< 当前请求段质量索引
  int64_t m_lastSegmentIndex;      //!< 最后一个段索引，总段数-1
  int64_t m_segmentCounter;        //!< 下一个下载段索引

  int64_t m_transmissionStartReceivingSegment;  //!< 段传输开始时间（微秒）
  int64_t m_transmissionEndReceivingSegment;    //!< 段传输结束时间（微秒）
  int64_t m_bytesReceived;                      //!< 当前包已接收字节数
  int64_t m_bDelay;            //!< 播放时最小缓冲区水平，下一下载开始前
  int64_t m_highestRepIndex;   //!< 最高表示级别索引
  uint64_t m_segmentDuration;  //!< 段持续时间（微秒）

  std::ofstream adaptationLog;      //!< 自适应日志输出流
  std::ofstream downloadLog;        //!< 下载日志输出流
  std::ofstream playbackLog;        //!< 播放日志输出流
  std::ofstream bufferLog;          //!< 缓冲区日志输出流
  std::ofstream throughputLog;      //!< 吞吐量日志输出流
  std::ofstream bufferUnderrunLog;  //!< 缓冲区下溢日志输出流

  uint64_t m_downloadRequestSent;  //!< 记录发送下载请求的时间

  throughputData m_throughput;  //!< 吞吐量跟踪
  bufferData m_bufferData;      //!< 缓冲区等级跟踪
  playbackData m_playbackData;  //!< 播放段跟踪
  videoData m_videoData;        //!< 段大小、平均码率和段持续时间信息
};

}  // namespace ns3

#endif /* TCP_STREAM_CLIENT_H */