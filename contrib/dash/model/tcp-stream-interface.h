#ifndef TCP_STREAM_INTERFACE_H
#define TCP_STREAM_INTERFACE_H

namespace ns3 {

// DASH 日志文件目录
std::string const dashLogDirectory = "dash-log-files/";

/*! \class algorithmReply tcp-stream-interface.h "model/tcp-stream-interface.h"
 *  \ingroup tcpStream
 *  \brief 存储自适应算法返回给客户端的响应数据
 *
 * 数据结构，表示自适应算法返回给客户端的信息，包含客户端
 * 决定下一步请求哪个表示层（representation index）以及
 * 下一次请求的时间间隔（微秒）的所有信息。
 * 可选字段用于记录日志：算法做出决策的时间以及决策代码的来源。
 */
struct algorithmReply {
  int64_t nextRepIndex;       //!< 下一个要下载的视频片段的表示层索引
  int64_t nextDownloadDelay;  //!< 下一次请求片段的延迟时间（微秒）
  int64_t decisionTime;       //!< 算法做出下载决策的时间（微秒），仅用于日志
  int64_t decisionCase;       //!< 算法做出决策的代码部分，用于日志
  int64_t delayDecisionCase;  //!< 决定延迟多少微秒请求片段的代码部分，用于日志
};

/*! \class throughputData tcp-stream-interface.h "model/tcp-stream-interface.h"
 *  \ingroup tcpStream
 *  \brief 存储吞吐量数据
 *
 * 该结构体包含客户端提供给自适应算法的吞吐量信息，
 * 用于计算下一步要请求的表示层。
 */
struct throughputData {
  std::vector<int64_t> transmissionRequested;  //!< 客户端请求片段的模拟时间（微秒）
  std::vector<int64_t> transmissionStart;      //!< 收到片段第一个数据包的模拟时间（微秒）
  std::vector<int64_t> transmissionEnd;        //!< 收到片段最后一个数据包的模拟时间（微秒）
  std::vector<int64_t> bytesReceived;          //!< 接收的字节数，即片段大小
};

/*! \class bufferData tcp-stream-interface.h "model/tcp-stream-interface.h"
 *  \ingroup tcpStream
 *  \brief 存储缓冲区数据
 *
 * 追踪缓冲区的状态。
 */
struct bufferData {
  std::vector<int64_t> timeNow;         //!< 当前模拟时间（微秒）
  std::vector<int64_t> bufferLevelOld;  //!< 添加刚下载片段前的缓冲区长度（微秒）
  std::vector<int64_t> bufferLevelNew;  //!< 添加刚下载片段后的缓冲区长度（微秒）
};

/*! \class videoData tcp-stream-interface.h "model/tcp-stream-interface.h"
 *  \ingroup tcpStream
 *  \brief 存储视频数据
 *
 * MPEG-DASH 媒体呈现描述（MPD）的简化版本，包含一个二维矩阵 [i][j]：
 * - i 表示视频的表示层（representation level）
 * - j 表示该表示层下每个片段的大小（字节）
 * 还包含每个表示层的平均码率以及片段时长（微秒）。
 */
struct videoData {
  std::vector<std::vector<int64_t>>
      segmentSize;                     //!< 第一维表示层，第二维存储对应片段的大小（字节）
  std::vector<double> averageBitrate;  //!< 每个表示层的平均码率（比特）
  int64_t segmentDuration;             //!< 片段时长（微秒）
};

struct audioData {
  std::vector<std::vector<int64_t>>
      segmentSize;                     //!< 第一维表示层，第二维存储对应片段的大小（字节）
  std::vector<double> averageBitrate;  //!< 每个表示层的平均码率（比特）
  int64_t segmentDuration;             //!< 片段时长（微秒）
};

/*! \class playbackData tcp-stream-interface.h "model/tcp-stream-interface.h"
 *  \ingroup tcpStream
 *  \brief 存储播放数据
 *
 * 包含一对值：
 * - playbackIndex 表示片段索引（乘以片段时长得到视频时间轴位置）
 * - playbackStart 表示该片段在模拟时间中的播放开始时间（微秒）
 */
struct playbackData {
  std::vector<int64_t> playbackIndex;  //!< 视频片段索引
  std::vector<int64_t> playbackStart;  //!< 该片段播放开始的模拟时间（微秒）
};

}  // namespace ns3

#endif /* TCP_STREAM_CLIENT_H */