// shared-bottleneck-detection.cc

#include "shared-bottleneck-detection.h"

#include <algorithm>

#include "ns3/log.h"
#include "ns3/simulator.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("SharedBottleneckDetection");

// 默认检测最近2s的数据，相似度的阈值为0.7，窗口偏移量随流的rtt变化而变化
SharedBottleneckDetection::SharedBottleneckDetection()
    : m_window(Seconds(2.0)), m_threshold(0.7), m_maxLagSamples(3) {}

void SharedBottleneckDetection::SetWindow(Time window) { m_window = window; }
Time SharedBottleneckDetection::GetWindow() { return m_window; }

void SharedBottleneckDetection::SetThreshold(double threshold) {
  m_threshold = threshold;
}

void SharedBottleneckDetection::SetMaxLagSamples(uint32_t maxLag) {
  m_maxLagSamples = maxLag;
}
int32_t SharedBottleneckDetection::GetMaxLag() { return m_maxLagSamples; }

// 每次检测之前都需要依据rtt，实时更新窗口偏移量
void SharedBottleneckDetection::UpdataMaxLag(const std::deque<RttEvent>& rttsA,
                                             const std::deque<RttEvent>& rttsB,
                                             const Time& RttSampleInterval,
                                             const size_t& m_maxRttSamples) {
  Time now = Simulator::Now();
  m_RttSampleInterval = RttSampleInterval;
  int32_t A_min = std::numeric_limits<int32_t>::max();
  int32_t A_max = 0;
  int32_t B_min = std::numeric_limits<int32_t>::max();
  int32_t B_max = 0;

  for (auto it = rttsA.rbegin(); it != rttsA.rend(); ++it) {
    if (now - it->rxTime <= m_window) {
      A_min = std::min(A_min, it->rtt);
      A_max = std::max(A_max, it->rtt);
    } else
      break;
  }

  for (auto it = rttsB.rbegin(); it != rttsB.rend(); ++it) {
    if (now - it->rxTime <= m_window) {
      B_min = std::min(B_min, it->rtt);
      B_max = std::max(B_max, it->rtt);
    } else
      break;
  }

  // 最大允许的偏移数，不能超过所有获取到的数据窗口长度
  maxPossibleLagSamples = m_maxRttSamples;

  int32_t maxDiff = std::max(std::abs(A_max - B_min), std::abs(A_min - B_max));

  m_maxLagSamples = std::min(
      static_cast<int32_t>(std::ceil(static_cast<double>(maxDiff) /
                                     m_RttSampleInterval.GetMilliSeconds())),
      maxPossibleLagSamples - 5);

  // m_maxLagSamples = 3;
}

// 效果不好，偏移的太小了，时间窗口太小了，应该维护一个50s-70s乃至更大的时间窗口，这样即使窗口偏移很大，也有较好的效果

// 获取最近的rtt数据
std::deque<RttEvent>
SharedBottleneckDetection::ExtractWindow(const std::deque<RttEvent>& rtts,
                                         Time window) const {
  std::deque<RttEvent> res;
  Time now = Simulator::Now();

  for (auto it = rtts.rbegin(); it != rtts.rend(); ++it) {
    if (now - it->rxTime <= window) {
      res.push_front(*it);
    } else {
      break;
    }
  }
  return res;
}

// 去掉基准线，只关注变化值
std::vector<double> SharedBottleneckDetection::ComputeDeltaRtt(
    const std::deque<RttEvent>& rtts) const {
  std::vector<double> delta;
  // 少于 1s 不作处理
  if (rtts.size() < 10) {
    NS_LOG_INFO("最近段时间窗口的数据太少了，不足以计算，只有： "
                << rtts.size());
    return delta;
  }

  for (size_t i = 1; i < rtts.size(); ++i) {
    delta.push_back(static_cast<double>(rtts[i].rtt) -
                    static_cast<double>(rtts[i - 1].rtt));
  }
  return delta;
}

// 计算 Pearson 相关系数
double SharedBottleneckDetection::PearsonCorrelation(
    const std::vector<double>& x, const std::vector<double>& y) const {
  if (x.size() != y.size() || x.empty()) return 0.0;

  double mean_x = 0.0, mean_y = 0.0;
  for (size_t i = 0; i < x.size(); ++i) {
    mean_x += x[i];
    mean_y += y[i];
  }
  mean_x /= x.size();
  mean_y /= y.size();

  double num = 0.0, den_x = 0.0, den_y = 0.0;
  for (size_t i = 0; i < x.size(); ++i) {
    double dx = x[i] - mean_x;
    double dy = y[i] - mean_y;
    num += dx * dy;
    den_x += dx * dx;
    den_y += dy * dy;
  }

  if (den_x == 0.0 || den_y == 0.0) return 0.0;

  return num / std::sqrt(den_x * den_y);
}

// 带有时延偏移的最大 Pearson 相关系数
double
SharedBottleneckDetection::MaxLaggedCorrelation(const std::vector<double>& x,
                                                const std::vector<double>& y,
                                                uint32_t maxLag) const {
  size_t n = std::min(x.size(), y.size());
  if (n < maxLag + 2) {
    NS_LOG_INFO("数据太少:" << n << "   采样率偏移过大:" << maxLag);
    return 0.0;
  }

  double best = -1;

  for (int lag = -static_cast<int>(maxLag); lag <= static_cast<int>(maxLag);
       ++lag) {
    std::vector<double> xs, ys;

    for (size_t i = 0; i < n; ++i) {
      int j = static_cast<int>(i) + lag;
      if (j >= 0 && j < static_cast<int>(n)) {
        xs.push_back(x[i]);
        ys.push_back(y[j]);
      }
    }

    if (xs.size() >= 5) {
      double rho = PearsonCorrelation(xs, ys);
      best = std::max(best, rho);
    }
  }

  return best;
}

// 对外的共享瓶颈检测函数
std::pair<bool, double>
SharedBottleneckDetection::Detect(const std::deque<RttEvent>& rttsA,
                                  const std::deque<RttEvent>& rttsB) {
  // 1. 滑动窗口
  Time window = m_window + m_maxLagSamples * m_RttSampleInterval;
  NS_LOG_INFO("[SBD] window=" << window.GetSeconds());

  auto winA = ExtractWindow(rttsA, window);
  auto winB = ExtractWindow(rttsB, window);

  // 2. 去基线
  auto deltaA = ComputeDeltaRtt(winA);
  auto deltaB = ComputeDeltaRtt(winB);

  // 3. 带时延偏移的相关性
  double rho = MaxLaggedCorrelation(deltaA, deltaB, m_maxLagSamples);
  NS_LOG_INFO("[SBD] rho=" << rho);

  // 4. 利用状态机判断
  bool res = UpdateAndCheck(rho);
  return {res, rho};
}

bool SharedBottleneckDetection::UpdateAndCheck(double rho) {
  Time now = Simulator::Now();

  const double ENTER_TH = 0.65;
  const double EXIT_TH = 0.4;
  const uint32_t K_ENTER = 3;
  const uint32_t K_EXIT = 5;

  if (m_state == SbdState::NOT_SHARED) {
    if (rho >= ENTER_TH) {
      m_enterCount++;
      if (m_enterCount >= K_ENTER) {
        m_state = SbdState::SHARED;
        m_enterCount = 0;
        NS_LOG_INFO("[SBD] ENTER shared bottleneck");
      }
    } else {
      m_enterCount = 0;
    }
  } else {  // SHARED
    if (rho <= EXIT_TH) {
      m_exitCount++;
      if (m_exitCount >= K_EXIT) {
        m_state = SbdState::NOT_SHARED;
        m_exitCount = 0;
        NS_LOG_INFO("[SBD] EXIT shared bottleneck");
      }
    } else {
      m_exitCount = 0;
    }
  }

  return m_state == SbdState::SHARED;
}

}  // namespace ns3