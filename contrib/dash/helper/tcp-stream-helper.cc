#include "tcp-stream-helper.h"  // 引入当前头文件，定义 TcpStreamClientHelper 和 TcpStreamServerHelper

#include "ns3/names.h"              // 引入 ns3 的 Names 模块，用于通过字符串名称查找节点对象
#include "ns3/tcp-stream-client.h"  // 引入 TcpStreamClient 类定义
#include "ns3/tcp-stream-server.h"  // 引入 TcpStreamServer 类定义
#include "ns3/uinteger.h"           // 引入 UintegerValue 类，用于设置整数类型属性

namespace ns3 {  // 所有 NS-3 相关类都在 ns3 命名空间下

// ==================== TcpStreamServerHelper 部分 ====================

// 构造函数：创建 TcpStreamServerHelper 对象，同时指定服务器监听端口
TcpStreamServerHelper::TcpStreamServerHelper(uint16_t port) {
  m_factory.SetTypeId(TcpStreamServer::GetTypeId());  // 设置工厂对象的类型为 TcpStreamServer
  SetAttribute("Port", UintegerValue(port));  // 设置 TcpStreamServer 的 "Port" 属性为传入的端口号
}

// 设置服务器属性（通用接口）
void TcpStreamServerHelper::SetAttribute(std::string name, const AttributeValue &value) {
  m_factory.Set(name, value);  // 将属性名和属性值记录到对象工厂中
}

// 在指定节点上安装服务器应用
ApplicationContainer TcpStreamServerHelper::Install(Ptr<Node> node) const {
  return ApplicationContainer(
      InstallPriv(node));  // 调用私有函数 InstallPriv 并返回封装好的 ApplicationContainer
}

// 在指定名称的节点上安装服务器应用
ApplicationContainer TcpStreamServerHelper::Install(std::string nodeName) const {
  Ptr<Node> node = Names::Find<Node>(nodeName);    // 通过节点名称查找节点对象
  return ApplicationContainer(InstallPriv(node));  // 调用 InstallPriv 安装并返回应用容器
}

// 在 NodeContainer 中的每个节点上安装服务器应用
ApplicationContainer TcpStreamServerHelper::Install(NodeContainer c) const {
  ApplicationContainer apps;  // 创建空的 ApplicationContainer
  for (NodeContainer::Iterator i = c.Begin(); i != c.End();
       ++i) {                   // 遍历 NodeContainer 中的每个节点
    apps.Add(InstallPriv(*i));  // 在每个节点上安装服务器应用，并添加到容器
  }
  return apps;  // 返回所有安装好的应用容器
}

// 私有函数：实际创建服务器应用并添加到节点
Ptr<Application> TcpStreamServerHelper::InstallPriv(Ptr<Node> node) const {
  Ptr<Application> app = m_factory.Create<TcpStreamServer>();  // 使用工厂创建 TcpStreamServer 对象
  node->AddApplication(app);                                   // 将创建的服务器应用添加到节点中
  return app;                                                  // 返回应用指针
}

// ==================== TcpStreamClientHelper 部分 ====================

// 构造函数：指定服务器的 Address 类型 IP 和端口
TcpStreamClientHelper::TcpStreamClientHelper(Address address, uint16_t port) {
  m_factory.SetTypeId(TcpStreamClient::GetTypeId());     // 设置工厂对象类型为 TcpStreamClient
  SetAttribute("RemoteAddress", AddressValue(address));  // 设置客户端要连接的远程服务器地址
  SetAttribute("RemotePort", UintegerValue(port));       // 设置客户端要连接的远程服务器端口
}

// 构造函数：指定服务器的 IPv4 地址和端口
TcpStreamClientHelper::TcpStreamClientHelper(Ipv4Address address, uint16_t port) {
  m_factory.SetTypeId(TcpStreamClient::GetTypeId());  // 设置工厂对象类型为 TcpStreamClient
  SetAttribute("RemoteAddress", AddressValue(Address(address)));  // 设置远程 IPv4 地址
  SetAttribute("RemotePort", UintegerValue(port));                // 设置远程端口
}

// 构造函数：指定服务器的 IPv6 地址和端口
TcpStreamClientHelper::TcpStreamClientHelper(Ipv6Address address, uint16_t port) {
  m_factory.SetTypeId(TcpStreamClient::GetTypeId());  // 设置工厂对象类型为 TcpStreamClient
  SetAttribute("RemoteAddress", AddressValue(Address(address)));  // 设置远程 IPv6 地址
  SetAttribute("RemotePort", UintegerValue(port));                // 设置远程端口
}

// 设置客户端属性（通用接口）
void TcpStreamClientHelper::SetAttribute(std::string name, const AttributeValue &value) {
  m_factory.Set(name, value);  // 将属性名和属性值记录到对象工厂
}

// 安装客户端应用到一组节点，每个节点对应一个自适应算法
ApplicationContainer
TcpStreamClientHelper::Install(std::vector<std::pair<Ptr<Node>, std::string> > clients) const {
  ApplicationContainer apps;                   // 创建空的应用容器
  for (uint i = 0; i < clients.size(); i++) {  // 遍历每个节点-算法对
    // 调用私有函数安装客户端应用，并将应用添加到容器中
    apps.Add(InstallPriv(clients.at(i).first, clients.at(i).second, i));
  }
  return apps;  // 返回所有安装好的客户端应用
}

// 私有函数：在节点上安装客户端应用并初始化自适应算法
Ptr<Application>
TcpStreamClientHelper::InstallPriv(Ptr<Node> node, std::string algo, uint16_t clientId) const {
  Ptr<Application> app = m_factory.Create<TcpStreamClient>();  // 创建 TcpStreamClient 对象
  app->GetObject<TcpStreamClient>()->SetAttribute(
      "ClientId", UintegerValue(clientId));                       // 设置客户端 ID，用于日志区分
  app->GetObject<TcpStreamClient>()->Initialise(algo, clientId);  // 初始化自适应算法
  node->AddApplication(app);                                      // 将客户端应用添加到节点
  return app;                                                     // 返回应用指针
}

}  // namespace ns3