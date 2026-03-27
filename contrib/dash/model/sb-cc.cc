#include "sb-cc.h"

#include "ns3/log.h"
#include "ns3/simulator.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("SbCc");

SbCc::SbCc()
    : m_wr(MilliSeconds(850)),
      m_halfWr(MilliSeconds(475)),
      m_state(MONITOR),
      m_shared(false),
      m_hasAudioCe(false),
      m_hasVideoCe(false),
      m_lastAudioCe(Time(0)),
      m_lastVideoCe(Time(0)),
      m_judgementTimeout(MilliSeconds(3500))  // 可调，先取 2*wr
{}

void SbCc::SetAudioFlow(Ipv4Address ip, uint16_t port) {
  m_audio.ip = ip;
  m_audio.port = port;
}

void SbCc::SetVideoFlow(Ipv4Address ip, uint16_t port) {
  m_video.ip = ip;
  m_video.port = port;
}

void SbCc::SetWindow(Time wr) {
  m_wr = wr;
  m_halfWr = wr / 2;
  m_judgementTimeout = wr * 2;
}

bool SbCc::IsSharedBottleneck() const { return m_shared; }

SbCc::State SbCc::GetState() const { return m_state; }

bool SbCc::CanFormPair(Time audioTime, Time videoTime) const {
  Time diff =
      audioTime > videoTime ? (audioTime - videoTime) : (videoTime - audioTime);
  return diff <= m_halfWr;
}

SbCc::PairMatch SbCc::BuildPair(Time audioTime, Time videoTime) const {
  PairMatch p;
  if (!CanFormPair(audioTime, videoTime)) {
    return p;
  }

  p.valid = true;
  p.audioTime = audioTime;
  p.videoTime = videoTime;
  p.pairTime = (audioTime > videoTime) ? audioTime : videoTime;
  return p;
}

void SbCc::EnterJudgement(const PairMatch& firstPair) {
  m_state = JUDGEMENT;
  m_firstPair = firstPair;

  NS_LOG_UNCOND("[SB-CC] Enter JUDGEMENT at "
                << firstPair.pairTime.GetSeconds()
                << "s, first pair: audio=" << firstPair.audioTime.GetSeconds()
                << "s, video=" << firstPair.videoTime.GetSeconds() << "s");
}

void SbCc::ConfirmSharedBottleneck(const PairMatch& secondPair) {
  m_shared = true;

  NS_LOG_UNCOND("[SB-CC] Shared bottleneck CONFIRMED at "
                << secondPair.pairTime.GetSeconds()
                << "s, second pair: audio=" << secondPair.audioTime.GetSeconds()
                << "s, video=" << secondPair.videoTime.GetSeconds() << "s");
}

void SbCc::Reset() {
  NS_LOG_UNCOND("[SB-CC] Reset to MONITOR at " << Simulator::Now().GetSeconds()
                                               << "s");

  m_state = MONITOR;
  m_shared = false;
  m_firstPair.valid = false;
}

void SbCc::ExpireIfNeeded(Time now) {
  if (m_state == JUDGEMENT && m_firstPair.valid) {
    if (now - m_firstPair.pairTime > m_judgementTimeout) {
      Reset();
    }
  }
}

void SbCc::HandleAudioCe(Time t) {
  m_hasAudioCe = true;
  m_lastAudioCe = t;

  if (!m_hasVideoCe) {
    return;
  }

  PairMatch pair = BuildPair(m_lastAudioCe, m_lastVideoCe);
  if (!pair.valid) {
    return;
  }

  if (m_state == MONITOR) {
    EnterJudgement(pair);
    return;
  }

  if (m_state == JUDGEMENT) {
    // 第二次匹配对必须晚于第一次
    if (pair.pairTime > m_firstPair.pairTime) {
      ConfirmSharedBottleneck(pair);
    }
  }
}

void SbCc::HandleVideoCe(Time t) {
  m_hasVideoCe = true;
  m_lastVideoCe = t;

  if (!m_hasAudioCe) {
    return;
  }

  PairMatch pair = BuildPair(m_lastAudioCe, m_lastVideoCe);
  if (!pair.valid) {
    return;
  }

  if (m_state == MONITOR) {
    EnterJudgement(pair);
    return;
  }

  if (m_state == JUDGEMENT) {
    if (pair.pairTime > m_firstPair.pairTime) {
      ConfirmSharedBottleneck(pair);
    }
  }
}

void SbCc::OnCeEvent(Ipv4Address srcIp, uint16_t srcPort, Time rxTime) {
  ExpireIfNeeded(rxTime);

  if (m_audio.Match(srcIp, srcPort)) {
    NS_LOG_INFO("[SB-CC] audio CE at " << rxTime.GetSeconds() << "s");
    HandleAudioCe(rxTime);
    return;
  }

  if (m_video.Match(srcIp, srcPort)) {
    NS_LOG_INFO("[SB-CC] video CE at " << rxTime.GetSeconds() << "s");
    HandleVideoCe(rxTime);
    return;
  }

  // 两流版：其他流忽略
}

}  // namespace ns3