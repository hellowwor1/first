#ifndef SB_CC_H
#define SB_CC_H

#include "ns3/ipv4-address.h"
#include "ns3/log.h"
#include "ns3/nstime.h"
#include "ns3/object.h"

namespace ns3 {

class SbCc {
 public:
  enum State { MONITOR = 0, JUDGEMENT };

  SbCc();
  ~SbCc() = default;

  void SetAudioFlow(Ipv4Address ip, uint16_t port);
  void SetVideoFlow(Ipv4Address ip, uint16_t port);
  void SetWindow(Time wr);

  // 实时输入一个 CE 事件
  void OnCeEvent(Ipv4Address srcIp, uint16_t srcPort, Time rxTime);

  bool IsSharedBottleneck() const;
  State GetState() const;

 private:
  struct FlowKey {
    Ipv4Address ip;
    uint16_t port = 0;

    bool Match(Ipv4Address otherIp, uint16_t otherPort) const {
      return ip == otherIp && port == otherPort;
    }

    bool IsValid() const { return port != 0; }
  };

  struct PairMatch {
    bool valid = false;
    Time audioTime = Time(0);
    Time videoTime = Time(0);
    Time pairTime = Time(0);  // 取两者较晚时刻
  };

  void HandleAudioCe(Time t);
  void HandleVideoCe(Time t);

  bool CanFormPair(Time audioTime, Time videoTime) const;
  PairMatch BuildPair(Time audioTime, Time videoTime) const;

  void EnterJudgement(const PairMatch& firstPair);
  void ConfirmSharedBottleneck(const PairMatch& secondPair);
  void Reset();

  void ExpireIfNeeded(Time now);

 private:
  FlowKey m_audio;
  FlowKey m_video;

  Time m_wr;      // 总窗口，建议 350ms
  Time m_halfWr;  // 半窗口，175ms

  State m_state;
  bool m_shared;

  bool m_hasAudioCe;
  bool m_hasVideoCe;
  Time m_lastAudioCe;
  Time m_lastVideoCe;

  // judgement 阶段保存第一次匹配对
  PairMatch m_firstPair;

  // 超时控制：避免一直卡在 judgement
  Time m_judgementTimeout;
};

}  // namespace ns3

#endif  // SB_CC_H