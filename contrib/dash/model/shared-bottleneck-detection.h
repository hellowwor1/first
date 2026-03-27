#ifndef SHARED_BOTTLENECK_DETECTION_H
#define SHARED_BOTTLENECK_DETECTION_H

#include <cmath>
#include <deque>
#include <vector>

#include "ns3/nstime.h"
#include "rtt.h"

namespace ns3 {
enum class SbdState { NOT_SHARED, SHARED };
/**
 * Shared Bottleneck Detection (SBD)
 * 基于 RTT 变化相关性的共享瓶颈检测模块
 * 旧版
 */
class SharedBottleneckDetection {
 public:
  SharedBottleneckDetection();
  ~SharedBottleneckDetection() = default;

  /* ================== 参数设置 ================== */

  void SetWindow(Time window);

  Time GetWindow();

  int32_t GetMaxLag();

  void SetThreshold(double threshold);
  void SetMaxLagSamples(uint32_t maxLag);

  /* ================== 核心接口 ================== */

  std::pair<bool, double>
  Detect(const std::deque<RttEvent>& rttsA, const std::deque<RttEvent>& rttsB);

  void
  UpdataMaxLag(const std::deque<RttEvent>& rttsA,
               const std::deque<RttEvent>& rttsB,
               const Time& m_RttSampleInterval, const size_t& m_maxRttSamples);

  bool UpdateAndCheck(double rho);

 private:
  /* ================== 内部算法函数 ================== */

  std::deque<RttEvent>
  ExtractWindow(const std::deque<RttEvent>& rtts, Time window) const;

  std::vector<double> ComputeDeltaRtt(const std::deque<RttEvent>& rtts) const;

  double PearsonCorrelation(const std::vector<double>& x,
                            const std::vector<double>& y) const;

  double
  MaxLaggedCorrelation(const std::vector<double>& x,
                       const std::vector<double>& y, uint32_t maxLag) const;

 private:
  /* ================== 参数 ================== */

  // 滑动窗口长度
  Time m_window;

  // 相关性阈值
  double m_threshold;

  // 最大时延偏移（采样点数）
  int32_t m_maxLagSamples;

  // 时延偏移上界
  int32_t maxPossibleLagSamples;

  // 采样间隔时间
  Time m_RttSampleInterval;

  // 检测状态
  SbdState m_state = SbdState::NOT_SHARED;

  // 达到进入阈值统计
  uint32_t m_enterCount = 0;

  // 达到退出阈值统计
  uint32_t m_exitCount = 0;

  // 进入阈值
  const double ENTER_TH = 0.7;

  // 退出阈值
  const double EXIT_TH = 0.4;

  // 进入状态所需达到阈值次数的要求
  const uint32_t K_ENTER = 3;

  // 退出状态所需达到阈值次数的要求
  const uint32_t K_EXIT = 5;
};

}  // namespace ns3

#endif  // SHARED_BOTTLENECK_DETECTION_H
