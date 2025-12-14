#ifndef MULTI_AV_STREAM_HELPER_H
#define MULTI_AV_STREAM_HELPER_H

#include <stdint.h>

#include <string>
#include <utility>
#include <vector>

#include "ns3/application-container.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv6-address.h"
#include "ns3/node-container.h"
#include "ns3/object-factory.h"

namespace ns3 {

/**
 * \ingroup multiTcpAvStream
 * \brief 创建多TCP流AV服务器应用的Helper类
 *
 * 支持同时创建视频服务器和音频服务器应用，分别监听不同端口
 */
class MultiTcpAvStreamServerHelper {
 public:
  /**
   * 创建MultiTcpAvStreamServerHelper，方便用户设置多流服务器仿真
   *
   * \param videoPort 视频服务器监听端口
   * \param audioPort 音频服务器监听端口
   */
  MultiTcpAvStreamServerHelper(uint16_t videoPort, uint16_t audioPort);

  /**
   * 创建MultiTcpAvStreamServerHelper，使用默认端口
   * 视频端口：10000，音频端口：10001
   */
  MultiTcpAvStreamServerHelper();

  /**
   * 设置在每个应用创建后要配置的属性
   *
   * \param name 要设置的属性名称
   * \param value 要设置的属性值
   */
  void SetAttribute(std::string name, const AttributeValue &value);

  /**
   * 在指定节点上创建一个MultiTcpAvStreamServerApplication
   *
   * \param node 指定创建应用的节点Ptr<Node>
   * \returns 返回一个ApplicationContainer，包含创建的应用
   */
  ApplicationContainer Install(Ptr<Node> node) const;

  /**
   * 在指定名称的节点上创建一个MultiTcpAvStreamServerApplication
   *
   * \param nodeName 指定创建应用的节点名称，该名称之前已在对象命名服务中注册
   * \returns 返回一个ApplicationContainer，包含创建的应用
   */
  ApplicationContainer Install(std::string nodeName) const;

  /**
   * \param c 要创建应用的节点，使用NodeContainer指定
   *
   * 在NodeContainer中的每个节点上创建一个多TCP流AV服务器应用
   * 每个节点将同时运行视频和音频服务器（监听不同端口）
   *
   * \returns 返回创建的应用，每个节点一个应用
   */
  ApplicationContainer Install(NodeContainer c) const;

  /**
   * 安装视频服务器到指定节点
   * \param node 目标节点
   * \param videoPort 视频服务器端口
   * \returns 返回安装的应用指针
   */
  Ptr<Application> InstallVideoServer(Ptr<Node> node, uint16_t videoPort) const;

  /**
   * 安装音频服务器到指定节点
   * \param node 目标节点
   * \param audioPort 音频服务器端口
   * \returns 返回安装的应用指针
   */
  Ptr<Application> InstallAudioServer(Ptr<Node> node, uint16_t audioPort) const;

 private:
  /**
   * 在节点上安装一个ns3::MultiTcpAvStreamServer，并使用SetAttribute设置的所有属性
   *
   * \param node 要安装MultiTcpAvStreamServer的节点
   * \returns 返回安装的应用Ptr
   */
  Ptr<Application> InstallPriv(Ptr<Node> node) const;

  ObjectFactory m_factory;  //!< 对象工厂
  uint16_t m_videoPort;     //!< 视频服务器端口
  uint16_t m_audioPort;     //!< 音频服务器端口
  bool m_useDefaultPorts;   //!< 是否使用默认端口
};

/**
 * \ingroup multiTcpAvStream
 * \brief 创建多TCP流AV客户端应用的Helper类
 *
 * 支持同时连接视频服务器和音频服务器，获取多源多路径的媒体流
 */
class MultiTcpAvStreamClientHelper {
 public:
  /**
   * 创建MultiTcpAvStreamClientHelper，方便用户设置多流客户端仿真
   *
   * \param videoIp 视频服务器的IP地址
   * \param videoPort 视频服务器的端口号
   * \param audioIp 音频服务器的IP地址
   * \param audioPort 音频服务器的端口号
   */
  MultiTcpAvStreamClientHelper(Address videoIp, uint16_t videoPort,
                               Address audioIp, uint16_t audioPort);

  /**
   * 创建MultiTcpAvStreamClientHelper，使用IPv4地址
   *
   * \param videoIp 视频服务器的IPv4地址
   * \param videoPort 视频服务器的端口号
   * \param audioIp 音频服务器的IPv4地址
   * \param audioPort 音频服务器的端口号
   */
  MultiTcpAvStreamClientHelper(Ipv4Address videoIp, uint16_t videoPort,
                               Ipv4Address audioIp, uint16_t audioPort);

  /**
   * 创建MultiTcpAvStreamClientHelper，使用IPv6地址
   *
   * \param videoIp 视频服务器的IPv6地址
   * \param videoPort 视频服务器的端口号
   * \param audioIp 音频服务器的IPv6地址
   * \param audioPort 音频服务器的端口号
   */
  MultiTcpAvStreamClientHelper(Ipv6Address videoIp, uint16_t videoPort,
                               Ipv6Address audioIp, uint16_t audioPort);

  /**
   * 设置在每个应用创建后要配置的属性
   *
   * \param name 要设置的属性名称
   * \param value 要设置的属性值
   */
  void SetAttribute(std::string name, const AttributeValue &value);

  /**
   * \param clients 节点及其对应使用的自适应算法名称
   *
   * 在输入节点上为每个节点创建一个多TCP流AV客户端应用，
   * 并根据给定字符串在每个客户端上实例化自适应算法
   *
   * \returns 返回创建的应用，每个输入节点一个应用
   */
  ApplicationContainer
  Install(std::vector<std::pair<Ptr<Node>, std::pair<std::string, std::string>>>
              clients) const;

  /**
   * 在单个节点上安装客户端应用
   * \param node 目标节点
   * \param algo 自适应算法名称
   * \param clientId 客户端ID
   * \returns 返回安装的应用指针
   */
  Ptr<Application> InstallSingleClient(Ptr<Node> node, std::string algo,
                                       uint16_t clientId) const;

  /**
   * 批量安装客户端应用到节点容器
   * \param nodes 节点容器
   * \param algo 自适应算法名称（所有客户端使用相同算法）
   * \returns 返回应用容器
   */
  ApplicationContainer
  InstallBatch(NodeContainer nodes, std::string algo) const;

 private:
  /**
   * 在节点上安装一个ns3::MultiTcpAvStreamClient，并使用SetAttribute设置的所有属性
   *
   * \param node 要安装MultiTcpAvStreamClient的节点
   * \param algo 包含该客户端使用的自适应算法名称的字符串
   * \param clientId 用于日志区分不同客户端对象
   * \returns 返回安装的应用Ptr
   */
  Ptr<Application> InstallPriv(Ptr<Node> node, std::string video_algo,
                               std::string audio_algo, uint16_t clientId) const;

  ObjectFactory m_factory;  //!< 对象工厂
  Address m_videoAddress;   //!< 视频服务器地址
  uint16_t m_videoPort;     //!< 视频服务器端口
  Address m_audioAddress;   //!< 音频服务器地址
  uint16_t m_audioPort;     //!< 音频服务器端口
};

}  // namespace ns3

#endif /* MULTI_AV_STREAM_HELPER_H */