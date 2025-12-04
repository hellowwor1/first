#ifndef TCP_STREAM_SERVER_H
#define TCP_STREAM_SERVER_H

#include <map>

#include "ns3/address.h"
#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/random-variable-stream.h"
#include "ns3/traced-callback.h"

namespace ns3 {

class Socket;
class Packet;
class PropagationDelayModel;

/**
 * \ingroup applications
 * \defgroup tcpStream TcpStream
 */

/**
 * \ingroup tcpStream
 * \brief 服务器用于管理每个客户端数据的数据结构。
 */
struct callbackData {
  uint32_t currentTxBytes;  //!< 已经发送的字节数，如果发送的字节数等于packetSizeToReturn，则设置为
                            //!< 0，表示该段传输完成
  uint32_t packetSizeToReturn;  //!< 需要返回给客户端的总字节数
  bool send;                    //!< 如果当前段还有未发送的字节，则为 true
};

/**
 * \ingroup tcpStream
 * \brief 一个 TCP 流服务器
 *
 * 客户端发送消息，指定希望服务器返回的字节数。
 */
class TcpStreamServer : public Application {
 public:
  /**
   * \brief 获取对象的类型 ID
   * \return 对象的 TypeId
   */
  static TypeId GetTypeId(void);

  TcpStreamServer();           //!< 构造函数
  virtual ~TcpStreamServer();  //!< 析构函数

 protected:
  virtual void DoDispose(void);  //!< 清理资源

 private:
  virtual void StartApplication(void);  //!< 启动应用程序
  virtual void StopApplication(void);   //!< 停止应用程序

  /**
   * \brief 处理收到的数据包，并将发送回调设置为 HandleSend
   *
   * 该函数由下层调用。收到的数据包内容通过
   * GetCommand(Ptr<Packet>packet)反序列化。
   * 如果数据包内容是一个整数字符串n，则服务器将返回 n 个字节给发送方。
   * \param socket 接收到数据包的套接字
   */
  void HandleRead(Ptr<Socket> socket);

  /**
   * \brief 向连接到 socket 的客户端发送 packetSizeToReturn 字节
   *
   * 该函数由 HandleRead 调用一次，在客户端请求发送 n 字节后执行。
   * 如果 n 大于 socket->GetTxAvailable()（即发送缓冲区剩余可用空间），
   * 则仅写入 socket->GetTxAvailable() 个字节到缓冲区。
   * 当缓冲区有空间释放时，会通过 SendCallback 再次调用此函数。
   * 对于该段和连接的客户端，已经发送的字节数存储在
   * m_callbackData[from].currentTxBytes 中，
   * 通过from（客户端地址）可访问该值。
   * m_callbackData[from].send表示服务器尚未发送完
   * m_callbackData[from].packetSizeToReturn 字节。 当发送完成时，将 send 置为
   * false，服务器将停止发送，直到客户端请求下一段。
   *
   * \param socket 接收到段请求的套接字，也是发送数据的套接字
   * \param packetSizeToReturn 需要返回给客户端的完整段大小（字节）
   */
  void HandleSend(Ptr<Socket> socket, uint32_t packetSizeToReturn);

  /**
   * \brief 设置接收和发送的回调函数
   * 将新连接的客户端加入 m_connectedClients，并为该客户端分配 callbackData 结构
   */
  void HandleAccept(Ptr<Socket> s, const Address& from);

  void HandlePeerClose(Ptr<Socket> socket);  //!< 处理客户端关闭连接
  void HandlePeerError(Ptr<Socket> socket);  //!< 处理客户端错误

  /**
   * \brief 反序列化客户端发送的数据
   * \param packet 客户端发送的数据包
   * \return 反序列化后的整数值
   */
  int64_t GetCommand(Ptr<Packet> packet);

  uint16_t m_port;        //!< 监听的端口
  Ptr<Socket> m_socket;   //!< IPv4 套接字
  Ptr<Socket> m_socket6;  //!< IPv6 套接字
  std::map<Address, callbackData>
      m_callbackData;  //!< 可以通过客户端地址访问当前已发送字节数、段大小和发送状态
  std::vector<Address> m_connectedClients;  //!< 存储当前已连接客户端的列表
};

}  // namespace ns3

#endif /* TCP_STREAM_SERVER_H */