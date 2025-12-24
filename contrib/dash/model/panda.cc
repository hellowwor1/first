/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * 这是 ns-3 DASH 中 PANDA ABR 算法的实现
 * PANDA = Probe-And-Adapt
 * 核心思想：主动探测可用带宽，而不是被动用 buffer 映射
 */

#include "panda.h"

namespace ns3 {

// 定义 ns-3 日志组件名
NS_LOG_COMPONENT_DEFINE("PandaAlgorithm");

// 将 PandaAlgorithm 注册为 ns-3 的 Object
NS_OBJECT_ENSURE_REGISTERED(PandaAlgorithm);

/*
 * 构造函数
 * 传入：
 *  - videoData：视频编码信息（码率、segment 时长等）
 *  - playbackData：播放状态
 *  - bufferData：缓冲区状态
 *  - throughputData：下载吞吐记录
 */
PandaAlgorithm::PandaAlgorithm(const videoData &videoData,
                               const playbackData &playbackData,
                               const bufferData &bufferData,
                               const throughputData &throughput)
    :  // 调用父类 AdaptationAlgorithm 构造函数
      AdaptationAlgorithm(videoData, playbackData, bufferData, throughput),

      // ===== PANDA 控制参数（来自论文）=====
      m_kappa(0.28),    // 带宽更新步长（探测强度）
      m_omega(0.3),     // 最小 margin（防止过度乐观）
      m_alpha(0.2),     // 平滑因子（低通滤波）
      m_beta(0.2),      // buffer 对请求间隔的影响系数
      m_epsilon(0.15),  // 上升冗余 margin
      m_bMin(26),       // 最小安全 buffer（秒）
      m_highestRepIndex(videoData.averageBitrate.size() - 1) {
  NS_LOG_INFO(this);

  // 确保至少有一个表示层
  NS_ASSERT_MSG(m_highestRepIndex >= 0,
                "The highest quality representation index should be => 0");
}

/*
 * GetNextRep：
 * 每下载完一个 segment 后，调用该函数决定：
 *  - 下一个 segment 的码率
 *  - 是否延迟发起下载请求
 */
algorithmReply
PandaAlgorithm::GetNextRep(const int64_t segmentCounter, int64_t clientId) {
  // 当前仿真时间（微秒）
  const int64_t timeNow = Simulator::Now().GetMicroSeconds();

  // 下载延迟（用于控制 ON-OFF 行为）
  int64_t delay = 0;

  /*
   * 第 0 个 segment：初始化
   */
  if (segmentCounter == 0) {
    m_lastVideoIndex = 0;                                // 初始最低码率
    m_lastBuffer = (m_videoData.segmentDuration) / 1e6;  // 初始 buffer
    m_lastTargetInterrequestTime = 0;

    algorithmReply answer;
    answer.nextRepIndex = 0;
    answer.nextDownloadDelay = 0;
    answer.decisionTime = timeNow;
    answer.decisionCase = 0;
    answer.delayDecisionCase = 0;
    return answer;
  }

  /*
   * ===== 1. 测量吞吐量 =====
   * throughputMeasured = 实际下载速率（Mbps）
   *
   * 计算方式：
   *   已下载数据量 / 下载耗时
   */
  double throughputMeasured =
      ((double)(m_videoData.averageBitrate.at(m_lastVideoIndex) *
                (m_videoData.segmentDuration / 1e6)) /
       (double)((m_throughput.transmissionEnd.back() -
                 m_throughput.transmissionRequested.back()) /
                1e6)) /
      1e6;

  /*
   * 第 1 个 segment：初始化带宽估计
   */
  if (segmentCounter == 1) {
    m_lastBandwidthShare = throughputMeasured;
    m_lastSmoothBandwidthShare = m_lastBandwidthShare;
  }

  /*
   * ===== 2. 实际请求间隔 =====
   * 如果下载完成得比预期慢，就用真实间隔
   */
  double actualInterrequestTime;
  if (timeNow - m_throughput.transmissionRequested.back() >
      m_lastTargetInterrequestTime * 1e6) {
    actualInterrequestTime =
        (timeNow - m_throughput.transmissionRequested.back()) / 1e6;
  } else {
    actualInterrequestTime = m_lastTargetInterrequestTime;
  }

  /*
   * ===== 3. 带宽 share 更新（PANDA 核心控制方程）=====
   *
   * 这是 PANDA 的“探测 + 回退”控制律
   * 类似 TCP 的 AIMD，但在应用层
   */
  double bandwidthShare =
      (m_kappa * (m_omega - std::max(0.0, m_lastBandwidthShare -
                                              throughputMeasured + m_omega))) *
          actualInterrequestTime +
      m_lastBandwidthShare;

  // 带宽不能为负
  if (bandwidthShare < 0) {
    bandwidthShare = 0;
  }
  m_lastBandwidthShare = bandwidthShare;

  /*
   * ===== 4. 平滑带宽估计 =====
   * 一阶低通滤波，抑制抖动
   */
  double smoothBandwidthShare =
      ((-m_alpha * (m_lastSmoothBandwidthShare - bandwidthShare)) *
       actualInterrequestTime) +
      m_lastSmoothBandwidthShare;

  m_lastSmoothBandwidthShare = smoothBandwidthShare;

  /*
   * ===== 5. 上下切换 margin =====
   * 上切更保守，下切更激进
   */
  double deltaUp = m_omega + m_epsilon * smoothBandwidthShare;
  double deltaDown = m_omega;

  // 找到满足带宽约束的最大码率
  int rUp = FindLargest(smoothBandwidthShare, segmentCounter - 1, deltaUp);
  int rDown = FindLargest(smoothBandwidthShare, segmentCounter - 1, deltaDown);

  /*
   * ===== 6. 三段式码率决策 =====
   */
  int videoIndex;
  if (m_videoData.averageBitrate.at(m_lastVideoIndex) <
      m_videoData.averageBitrate.at(rUp)) {
    // 可以安全升码率
    videoIndex = rUp;
  } else if (m_videoData.averageBitrate.at(rUp) <=
                 m_videoData.averageBitrate.at(m_lastVideoIndex) &&
             m_videoData.averageBitrate.at(m_lastVideoIndex) <=
                 m_videoData.averageBitrate.at(rDown)) {
    // 保持当前码率
    videoIndex = m_lastVideoIndex;
  } else {
    // 需要降码率
    videoIndex = rDown;
  }

  m_lastVideoIndex = videoIndex;

  /*
   * ===== 7. 计算目标请求间隔 =====
   *
   * 第一项：按带宽下载该 segment 需要的时间
   * 第二项：buffer 偏离安全值的修正项
   */
  double targetInterrequestTime =
      std::max(0.0, ((double)((m_videoData.averageBitrate.at(videoIndex) *
                               (m_videoData.segmentDuration / 1e6)) /
                              1e6) /
                     smoothBandwidthShare) +
                        m_beta * (m_lastBuffer - m_bMin));

  /*
   * ===== 8. 是否需要延迟请求 =====
   * 控制 ON-OFF 行为，避免 TCP 过度竞争
   */
  if (m_throughput.transmissionEnd.back() -
          m_throughput.transmissionRequested.back() <
      m_lastTargetInterrequestTime * 1e6) {
    delay = 1e6 * m_lastTargetInterrequestTime -
            (m_throughput.transmissionEnd.back() -
             m_throughput.transmissionRequested.back());
  } else {
    delay = 0;
  }

  m_lastTargetInterrequestTime = targetInterrequestTime;

  /*
   * ===== 9. 更新 buffer =====
   */
  m_lastBuffer = (m_bufferData.bufferLevelNew.back() -
                  (timeNow - m_throughput.transmissionEnd.back())) /
                 1e6;

  /*
   * ===== 返回决策 =====
   */
  algorithmReply answer;
  answer.nextRepIndex = videoIndex;
  answer.nextDownloadDelay = delay;
  answer.decisionTime = timeNow;
  answer.decisionCase = 0;
  answer.delayDecisionCase = 0;
  return answer;
}

/*
 * FindLargest：
 * 在所有表示层中，找 bitrate <= (带宽 - margin) 的最大 index
 */
int PandaAlgorithm::FindLargest(const double smoothBandwidthShare,
                                const int64_t segmentCounter,
                                const double delta) {
  int64_t largestBitrateIndex = 0;
  for (int i = 0; i <= m_highestRepIndex; i++) {
    int64_t currentBitrate = m_videoData.averageBitrate.at(i) / 1e6;
    if (currentBitrate <= (smoothBandwidthShare - delta)) {
      largestBitrateIndex = i;
    }
  }
  return largestBitrateIndex;
}

}  // namespace ns3
