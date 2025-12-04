#ifndef MULTI_AV_STREAM_SERVER_H
#define MULTI_AV_STREAM_SERVER_H

#include <map>
#include <string>

#include "ns3/address.h"
#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/random-variable-stream.h"
#include "ns3/traced-callback.h"

namespace ns3 {

class Socket;
class Packet;

/**
 * \ingroup applications
 * \defgroup multiTcpAvStream MultiTcpAvStream
 */

/**
 * \ingroup multiTcpAvStream
 * \brief 管理每个客户端流数据的数据结构
 *
 * 为多源多路径场景设计，支持视频流和音频流的独立管理
 */
struct MultiTcpAvCallbackData {
  uint32_t currentTxBytes;      //!< 当前流已发送的字节数
  uint32_t packetSizeToReturn;  //!< 需要返回给客户端的字节总数
  bool send;                    //!< 发送状态标志，true表示需要继续发送
  std::string streamType;       //!< 流类型标识："video" 或 "audio"
};

/**
 * \ingroup multiTcpAvStream
 * \brief 多TCP流AV服务器
 *
 * 为多源多路径传输场景设计，支持：
 * 1. 视频流和音频流分别处理
 * 2. 每个流使用独立的端口
 * 3. 客户端可以请求特定类型的流（视频或音频）
 */
class MultiTcpAvStreamServer : public Application {
 public:
  /**
   * \brief 获取对象的类型ID
   * \return 对象的TypeId
   */
  static TypeId GetTypeId(void);

  MultiTcpAvStreamServer();           //!< 构造函数
  virtual ~MultiTcpAvStreamServer();  //!< 析构函数

  /**
   * \brief 设置视频流监听端口
   * \param port 视频流端口号
   */
  void SetVideoPort(uint16_t port);

  /**
   * \brief 设置音频流监听端口
   * \param port 音频流端口号
   */
  void SetAudioPort(uint16_t port);

  /**
   * \brief 获取视频流监听端口
   * \return 视频流端口号
   */
  uint16_t GetVideoPort(void) const;

  /**
   * \brief 获取音频流监听端口
   * \return 音频流端口号
   */
  uint16_t GetAudioPort(void) const;

 protected:
  virtual void DoDispose(void);  //!< 清理资源

 private:
  virtual void StartApplication(void);  //!< 启动应用程序
  virtual void StopApplication(void);   //!< 停止应用程序

  /**
   * \brief 处理收到的数据包，并将发送回调设置为 HandleSend
   *
   * 解析客户端请求，获取请求的字节数和流类型
   * 收到的数据包内容通过GetCommand(Ptr<Packet>packet)反序列化。
   * 如果数据包内容是一个整数字符串n，则服务器将返回 n 个字节给发送方。
   * \param socket 接收到数据包的套接字
   */
  void HandleRead(Ptr<Socket> socket);

  /**
   * \brief 向连接到 socket 的客户端发送 packetSizeToReturn 字节
   *
   * 根据客户端请求的字节数发送数据，支持流式传输
   * \param socket 发送数据的套接字
   * \param packetSizeToReturn 需要发送的字节总数
   */
  void HandleSend(Ptr<Socket> socket, uint32_t packetSizeToReturn);

  /**
   * \brief 处理新连接请求
   *
   * 根据连接端口判断流类型（视频或音频）
   * \param s 新连接的套接字
   * \param from 客户端地址
   */
  void HandleAccept(Ptr<Socket> s, const Address& from);

  void HandlePeerClose(Ptr<Socket> socket);  //!< 处理客户端关闭连接
  void HandlePeerError(Ptr<Socket> socket);  //!< 处理客户端错误

  /**
   * \brief 反序列化客户端请求
   *
   * 解析客户端请求的字节数，格式为纯数字字符串，例如："1048576"
   * 服务器根据连接端口自动判断是视频请求还是音频请求
   * \param packet 客户端发送的数据包
   * \return 解析后的字节数
   */
  int64_t ParseCommand(Ptr<Packet> packet);

  /**
   * \brief 根据端口号获取流类型
   *
   * \param localPort 本地监听端口
   * \return 流类型字符串
   */
  std::string GetStreamTypeFromPort(uint16_t localPort);

  uint16_t m_videoPort;        //!< 视频流监听端口
  uint16_t m_audioPort;        //!< 音频流监听端口
  Ptr<Socket> m_videoSocket;   //!< 视频流IPv4套接字
  Ptr<Socket> m_videoSocket6;  //!< 视频流IPv6套接字
  Ptr<Socket> m_audioSocket;   //!< 音频流IPv4套接字
  Ptr<Socket> m_audioSocket6;  //!< 音频流IPv6套接字

  /**
   * \brief 客户端流数据映射
   *
   * 键：客户端 ip+port
   * 值：对应的回调数据结构
   */
  std::map<Address, MultiTcpAvCallbackData> m_callbackDataMap;

  /**
   * \brief 已连接客户端列表
   *
   * Address 值的表现形式为："ip:port"
   * 可以通过port轻松分辨是请求音频资源还是视频资源
   */
  std::vector<Address> m_connectedClients;
};

}  // namespace ns3

#endif /* MULTI_TCP_AV_STREAM_SERVER_H */