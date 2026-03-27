#ifndef FLOW_STATE_H
#define FLOW_STATE_H

#include <cstdint>

namespace ns3 {

/**
 * \brief Per-flow queue and loss state at router
 *
 * 用于在路由器（QueueDisc）侧维护：
 * - 当前队列中该流占用的 packet 数
 * - 在一个统计周期内该流被丢弃的 packet 数
 */
struct FlowQueueStat {
  uint32_t queuedPackets{0};    //!< 当前队列中该流的包数
  uint32_t droppedPackets{0};   //!< 本统计周期内被丢弃的包数
  uint32_t dequeuedPackets{0};  //!< 本统计周期内出队的包数
  uint32_t enqueuedPackets{0};  //!< 本统计周期内入队的包数
};

}  // namespace ns3

#endif  // FLOW_STATE_H
