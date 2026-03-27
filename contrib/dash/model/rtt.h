// rtt-event.h
#ifndef RTT_H
#define RTT_H

#include "ns3/nstime.h"

namespace ns3 {

/**
 * \brief 管理收到的Rtt信息
 */
struct RttEvent {
  Time rxTime;       // 客户端收到 CE 的时间
  uint16_t srcPort;  // 源端口（区分音频/视频流）
  int32_t rtt;
};
}  // namespace ns3

#endif  // RTT_EVENT_H
