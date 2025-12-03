#ifndef TCP_STREAM_HELPER_H
#define TCP_STREAM_HELPER_H

#include <stdint.h>

#include "ns3/application-container.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv6-address.h"
#include "ns3/node-container.h"
#include "ns3/object-factory.h"

namespace ns3 {

/**
 * \ingroup TcpStream
 * \brief 创建一个服务器应用，它等待输入的 UDP 数据包
 *        并将其发送回原始发送方。
 */
class TcpStreamServerHelper {
 public:
  /**
   * 创建 TcpStreamServerHelper，方便用户设置带有回显的仿真。
   *
   * \param port 服务器等待接收数据包的端口号
   */
  TcpStreamServerHelper(uint16_t port);

  /**
   * 设置在每个应用创建后要配置的属性。
   *
   * \param name 要设置的属性名称
   * \param value 要设置的属性值
   */
  void SetAttribute(std::string name, const AttributeValue &value);

  /**
   * 在指定节点上创建一个 TcpStreamServerApplication。
   *
   * \param node 指定创建应用的节点 Ptr<Node>。
   *
   * \returns 返回一个 ApplicationContainer，包含创建的应用。
   */
  ApplicationContainer Install(Ptr<Node> node) const;

  /**
   * 在指定名称的节点上创建一个 TcpStreamServerApplication
   *
   * \param nodeName 指定创建应用的节点名称，该名称之前已在对象命名服务中注册。
   *
   * \returns 返回一个 ApplicationContainer，包含创建的应用。
   */
  ApplicationContainer Install(std::string nodeName) const;

  /**
   * \param c 要创建应用的节点，使用 NodeContainer 指定。
   *
   * 在 NodeContainer 中的每个节点上创建一个 tcp 流服务器应用。
   *
   * \returns 返回创建的应用，每个节点一个应用。
   */
  ApplicationContainer Install(NodeContainer c) const;

 private:
  /**
   * 在节点上安装一个 ns3::TcpStreamServer，并使用 SetAttribute 设置的所有属性。
   *
   * \param node 要安装 TcpStreamServer 的节点
   * \returns 返回安装的应用 Ptr
   */
  Ptr<Application> InstallPriv(Ptr<Node> node) const;

  ObjectFactory m_factory;  //!< 对象工厂
};

/**
 * \ingroup TcpStream
 * \brief 创建一个应用，它发送 UDP 数据包并等待回显
 */
class TcpStreamClientHelper {
 public:
  /**
   * 创建 TcpStreamClientHelper，方便用户设置带有回显的仿真。
   *
   * \param ip 远程 tcp 流服务器的 IP 地址
   * \param port 远程 tcp 流服务器的端口号
   */
  TcpStreamClientHelper(Address ip, uint16_t port);
  /**
   * 创建 TcpStreamClientHelper，方便用户设置带有回显的仿真。
   *
   * \param ip 远程 tcp 流服务器的 IPv4 地址
   * \param port 远程 tcp 流服务器的端口号
   */
  TcpStreamClientHelper(Ipv4Address ip, uint16_t port);
  /**
   * 创建 TcpStreamClientHelper，方便用户设置带有回显的仿真。
   *
   * \param ip 远程 tcp 流服务器的 IPv6 地址
   * \param port 远程 tcp 流服务器的端口号
   */
  TcpStreamClientHelper(Ipv6Address ip, uint16_t port);

  /**
   * 设置在每个应用创建后要配置的属性。
   *
   * \param name 要设置的属性名称
   * \param value 要设置的属性值
   */
  void SetAttribute(std::string name, const AttributeValue &value);

  /**
   * \param clients 节点及其对应使用的自适应算法名称
   *
   * 在输入节点上为每个节点创建一个 tcp 流客户端应用，
   * 并根据给定字符串在每个客户端上实例化自适应算法。
   *
   * \returns 返回创建的应用，每个输入节点一个应用。
   */
  ApplicationContainer Install(std::vector<std::pair<Ptr<Node>, std::string> > clients) const;

 private:
  /**
   * 在节点上安装一个 ns3::TcpStreamClient，并使用 SetAttribute 设置的所有属性。
   *
   * \param node 要安装 TcpStreamClient 的节点
   * \param algo 包含该客户端使用的自适应算法名称的字符串
   * \param clientId 用于日志区分不同客户端对象
   * \param simulationId 用于日志区分不同仿真
   * \returns 返回安装的应用 Ptr
   */
  Ptr<Application> InstallPriv(Ptr<Node> node, std::string algo, uint16_t clientId) const;
  ObjectFactory m_factory;  //!< 对象工厂
};

}  // namespace ns3

#endif /* TCP_STREAM_HELPER_H */