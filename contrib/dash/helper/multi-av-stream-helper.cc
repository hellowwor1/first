// 包含多TCP流AV Helper的头文件
#include "multi-av-stream-helper.h"

// 引入ns3的Names模块，用于通过字符串名称查找节点对象
#include "ns3/names.h"
// 引入多TCP流AV客户端类定义
#include "ns3/multi-av-stream-client.h"
// 引入多TCP流AV服务器类定义
#include "ns3/multi-av-stream-server.h"
// 引入UintegerValue类，用于设置整数类型属性
#include "ns3/uinteger.h"

// 所有NS-3相关类都在ns3命名空间下
namespace ns3 {

NS_LOG_COMPONENT_DEFINE("MultiTcpAvStreamHelper");

// ==================== MultiTcpAvStreamServerHelper 部分 ====================

// 构造函数：创建MultiTcpAvStreamServerHelper对象，指定视频和音频端口
MultiTcpAvStreamServerHelper::MultiTcpAvStreamServerHelper(uint16_t videoPort,
                                                           uint16_t audioPort)
    : m_videoPort(videoPort),     // 初始化视频端口
      m_audioPort(audioPort),     // 初始化音频端口
      m_useDefaultPorts(false) {  // 标记为使用指定端口
  // 设置工厂对象的类型为MultiTcpAvStreamServer
  m_factory.SetTypeId(MultiTcpAvStreamServer::GetTypeId());
  // 设置视频端口属性
  SetAttribute("VideoPort", UintegerValue(videoPort));
  // 设置音频端口属性
  SetAttribute("AudioPort", UintegerValue(audioPort));
}

// 构造函数：使用默认端口（视频10000，音频10001）
MultiTcpAvStreamServerHelper::MultiTcpAvStreamServerHelper()
    : m_videoPort(10000),        // 默认视频端口
      m_audioPort(10001),        // 默认音频端口
      m_useDefaultPorts(true) {  // 标记为使用默认端口
  // 设置工厂对象的类型为MultiTcpAvStreamServer
  m_factory.SetTypeId(MultiTcpAvStreamServer::GetTypeId());
  // 设置默认视频端口属性
  SetAttribute("VideoPort", UintegerValue(m_videoPort));
  // 设置默认音频端口属性
  SetAttribute("AudioPort", UintegerValue(m_audioPort));
}

// 设置服务器属性（通用接口）
void MultiTcpAvStreamServerHelper::SetAttribute(std::string name,
                                                const AttributeValue &value) {
  m_factory.Set(name, value);  // 将属性名和属性值记录到对象工厂中
}

// 在指定节点上安装服务器应用
ApplicationContainer
MultiTcpAvStreamServerHelper::Install(Ptr<Node> node) const {
  // 调用私有函数InstallPriv并返回封装好的ApplicationContainer
  return ApplicationContainer(InstallPriv(node));
}

// 在指定名称的节点上安装服务器应用
ApplicationContainer
MultiTcpAvStreamServerHelper::Install(std::string nodeName) const {
  // 通过节点名称查找节点对象
  Ptr<Node> node = Names::Find<Node>(nodeName);
  // 调用InstallPriv安装并返回应用容器
  return ApplicationContainer(InstallPriv(node));
}

// 在NodeContainer中的每个节点上安装服务器应用
ApplicationContainer
MultiTcpAvStreamServerHelper::Install(NodeContainer c) const {
  ApplicationContainer apps;  // 创建空的ApplicationContainer
  // 遍历NodeContainer中的每个节点
  for (NodeContainer::Iterator i = c.Begin(); i != c.End(); ++i) {
    // 在每个节点上安装服务器应用，并添加到容器
    apps.Add(InstallPriv(*i));
  }
  return apps;  // 返回所有安装好的应用容器
}

// 私有函数：实际创建服务器应用并添加到节点
Ptr<Application>
MultiTcpAvStreamServerHelper::InstallPriv(Ptr<Node> node) const {
  // 使用工厂创建MultiTcpAvStreamServer对象
  Ptr<Application> app = m_factory.Create<MultiTcpAvStreamServer>();
  // 将创建的服务器应用添加到节点中
  node->AddApplication(app);
  return app;  // 返回应用指针
}

// ==================== MultiTcpAvStreamClientHelper 部分 ====================

// 构造函数：指定视频和音频服务器的Address类型IP和端口
MultiTcpAvStreamClientHelper::MultiTcpAvStreamClientHelper(Address videoAddress,
                                                           uint16_t videoPort,
                                                           Address audioAddress,
                                                           uint16_t audioPort)
    : m_videoAddress(videoAddress),  // 初始化视频服务器地址
      m_videoPort(videoPort),        // 初始化视频服务器端口
      m_audioAddress(audioAddress),  // 初始化音频服务器地址
      m_audioPort(audioPort) {       // 初始化音频服务器端口
  // 设置工厂对象类型为MultiTcpAvStreamClient
  m_factory.SetTypeId(MultiTcpAvStreamClient::GetTypeId());
  // 设置客户端要连接的视频服务器地址
  SetAttribute("VideoRemoteAddress", AddressValue(videoAddress));
  // 设置客户端要连接的视频服务器端口
  SetAttribute("VideoRemotePort", UintegerValue(videoPort));
  // 设置客户端要连接的音频服务器地址
  SetAttribute("AudioRemoteAddress", AddressValue(audioAddress));
  // 设置客户端要连接的音频服务器端口
  SetAttribute("AudioRemotePort", UintegerValue(audioPort));
}

// 构造函数：指定视频和音频服务器的IPv4地址和端口
MultiTcpAvStreamClientHelper::MultiTcpAvStreamClientHelper(Ipv4Address videoIp,
                                                           uint16_t videoPort,
                                                           Ipv4Address audioIp,
                                                           uint16_t audioPort)
    : m_videoAddress(Address(videoIp)),  // 将IPv4地址转换为通用Address类型
      m_videoPort(videoPort),            // 初始化视频服务器端口
      m_audioAddress(Address(audioIp)),  // 将IPv4地址转换为通用Address类型
      m_audioPort(audioPort) {           // 初始化音频服务器端口
  // 设置工厂对象类型为MultiTcpAvStreamClient
  m_factory.SetTypeId(MultiTcpAvStreamClient::GetTypeId());
  // 设置客户端要连接的视频服务器IPv4地址
  SetAttribute("VideoRemoteAddress", AddressValue(Address(videoIp)));
  // 设置客户端要连接的视频服务器端口
  SetAttribute("VideoRemotePort", UintegerValue(videoPort));
  // 设置客户端要连接的音频服务器IPv4地址
  SetAttribute("AudioRemoteAddress", AddressValue(Address(audioIp)));
  // 设置客户端要连接的音频服务器端口
  SetAttribute("AudioRemotePort", UintegerValue(audioPort));
}

// 构造函数：指定视频和音频服务器的IPv6地址和端口
MultiTcpAvStreamClientHelper::MultiTcpAvStreamClientHelper(Ipv6Address videoIp,
                                                           uint16_t videoPort,
                                                           Ipv6Address audioIp,
                                                           uint16_t audioPort)
    : m_videoAddress(Address(videoIp)),  // 将IPv6地址转换为通用Address类型
      m_videoPort(videoPort),            // 初始化视频服务器端口
      m_audioAddress(Address(audioIp)),  // 将IPv6地址转换为通用Address类型
      m_audioPort(audioPort) {           // 初始化音频服务器端口
  // 设置工厂对象类型为MultiTcpAvStreamClient
  m_factory.SetTypeId(MultiTcpAvStreamClient::GetTypeId());
  // 设置客户端要连接的视频服务器IPv6地址
  SetAttribute("VideoRemoteAddress", AddressValue(Address(videoIp)));
  // 设置客户端要连接的视频服务器端口
  SetAttribute("VideoRemotePort", UintegerValue(videoPort));
  // 设置客户端要连接的音频服务器IPv6地址
  SetAttribute("AudioRemoteAddress", AddressValue(Address(audioIp)));
  // 设置客户端要连接的音频服务器端口
  SetAttribute("AudioRemotePort", UintegerValue(audioPort));
}

// 设置客户端属性（通用接口）
void MultiTcpAvStreamClientHelper::SetAttribute(std::string name,
                                                const AttributeValue &value) {
  m_factory.Set(name, value);  // 将属性名和属性值记录到对象工厂
}

// 安装客户端应用到一组节点，每个节点对应一个自适应算法
ApplicationContainer MultiTcpAvStreamClientHelper::Install(
    std::vector<std::pair<Ptr<Node>, std::pair<std::string, std::string>>>
        clients) const {
  ApplicationContainer apps;  // 创建空的应用容器
  // 遍历每个节点-算法对
  for (uint32_t i = 0; i < clients.size(); i++) {
    // 调用私有函数安装客户端应用，并将应用添加到容器中
    apps.Add(InstallPriv(clients.at(i).first, clients.at(i).second.first,
                         clients.at(i).second.second, i));
  }
  return apps;  // 返回所有安装好的客户端应用
}

// 私有函数：在节点上安装客户端应用并初始化自适应算法
Ptr<Application>
MultiTcpAvStreamClientHelper::InstallPriv(Ptr<Node> node,
                                          std::string video_algo,
                                          std::string audio_algo,
                                          uint16_t clientId) const {
  // 使用工厂创建MultiTcpAvStreamClient对象
  Ptr<Application> app = m_factory.Create<MultiTcpAvStreamClient>();

  // 获取MultiTcpAvStreamClient指针
  Ptr<MultiTcpAvStreamClient> client = app->GetObject<MultiTcpAvStreamClient>();
  // 设置客户端ID，用于日志区分
  client->SetAttribute("ClientId", UintegerValue(clientId));
  // 初始化自适应算法
  client->Initialise(video_algo, audio_algo, clientId);
  // 将客户端应用添加到节点
  node->AddApplication(app);
  return app;  // 返回应用指针
}

}  // namespace ns3