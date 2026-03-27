#include "mptcp-sbd.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "ns3/log.h"
namespace ns3 {
NS_LOG_COMPONENT_DEFINE("MPTCP");
bool MPTCP_SBD::GetPB() const { return PB; }
void MPTCP_SBD::SetPB(bool value) { PB = value; }

void MPTCP_SBD::AddMeanOwd() {
  // 计算当前最后时间窗口的统计量（平均 OWD）

  // 当前没有数据，直接跳过
  if (m_Owds.empty()) {
    return;
  }
  auto& current_window = m_Owds.back().owds;
  // 当前窗口没有样本，直接跳过
  if (current_window.empty()) {
    return;
  }
  // 当前窗口最小值与最大值的差值大于平均OWD差值的10倍，说明当前窗口的值不值得用
  int64_t min_owd =
      *std::min_element(current_window.begin(), current_window.end());
  int64_t max_owd =
      *std::max_element(current_window.begin(), current_window.end());
  m_Owds.back().min_Owd = min_owd;
  m_Owds.back().max_Owd = max_owd;
  // if (m_MeanVar>0 && max_owd - min_owd > 10 * m_MeanVar) {
  //   // 当前窗口的值不值得用，直接跳过
  //   current_window.clear();  //
  //   清空当前窗口的样本，避免对后续窗口的统计造成干扰 return;
  // }
  // 计算平均 OWD
  double sum_owd =
      std::accumulate(current_window.begin(), current_window.end(), 0.0);
  double basemean_owd = sum_owd / current_window.size();
  m_Owds.back().meanOwd = basemean_owd;

  m_BaseMeanOwds.push_back(basemean_owd);
  m_MeanOwd =
      std::accumulate(m_BaseMeanOwds.begin(), m_BaseMeanOwds.end(), 0.0) /
      m_BaseMeanOwds.size();
  CalAll();
}

void MPTCP_SBD::DeleteMeanOwd() {
  // 删除最旧时间窗口的统计量（平均 OWD）

  // 当前没有数据，直接跳过
  if (m_BaseMeanOwds.empty() || m_Owds.empty()) {
    return;
  }
  const auto& first_window = m_Owds.front().owds;
  // 如果当前窗口没有样本,当初在计算Owd的时候就没有统计，直接跳过
  if (first_window.empty()) {
    return;
  }
  m_BaseMeanOwds.pop_front();
  if (m_BaseMeanOwds.empty()) {
    m_MeanOwd = 0.0;
  } else {
    m_MeanOwd =
        std::accumulate(m_BaseMeanOwds.begin(), m_BaseMeanOwds.end(), 0.0) /
        m_BaseMeanOwds.size();
    CalAll();
  }
}

void MPTCP_SBD::CalSkews() {
  // 计算每个时间窗口的 Skewness 并更新平均 Skewness
  double smaller = 0;
  double bigger = 0;
  double c_n = 0;
  for (const auto& window : m_Owds) {
    const auto& samples = window.owds;
    if (samples.size() <= 0) {
      continue;  // 样本数不足，跳过
    }
    c_n += samples.size();
    for (const auto& owd_c : samples) {
      if (owd_c < m_MeanOwd) {
        smaller++;
      } else if (owd_c > m_MeanOwd) {
        bigger++;
      }
    }
  }
  if (c_n > 0)
    m_MeanSkew = (smaller - bigger) / c_n;
  else
    m_MeanSkew = 0.0;
}

void MPTCP_SBD::CalVars() {
  // 计算每个时间窗口的 Variance 并更新平均 Variance
  double c_n = 0;
  double var_base_n = 0.0;

  // 遍历每个时间窗口
  for (size_t i = 1; i < m_Owds.size(); ++i) {
    const auto& window = m_Owds[i];
    const auto& samples = window.owds;
    if (samples.size() <= 0) {
      // 空窗口,不统计variance
      continue;
    }
    if (m_Owds[i - 1].owds.size() <= 0) {
      // 前一个窗口为空,不统计variance
      continue;
    }
    // 只有当前窗口+前一个窗口不为空，才统计variance
    c_n += samples.size();

    // 获取前一个窗口的平均 OWD (OWD_{n-1})
    // 如果是第一个窗口，则没有前一个窗口的平均 OWD，特殊处理
    double prev_window_mean = m_Owds[i - 1].meanOwd;

    // 计算 sum_{c=1}^{C_n} |OWD_c - OWD_{n-1}|
    for (const auto& owd_c : samples) {
      var_base_n += std::abs(static_cast<double>(owd_c) - prev_window_mean);
    }
  }
  // 计算所有时间窗口的平均 Variance: var_est = sum(var_base_n) / sum(C_n)
  if (c_n > 0)
    m_MeanVar = var_base_n / c_n;
  else
    m_MeanVar = 0.0;
}

void MPTCP_SBD::CalFreqs() {
  // 遍历每个时间窗口
  double ans = 0;
  double n = 0;
  for (size_t i = 1; i < m_Owds.size(); ++i) {
    bool flag = false;
    const auto& window = m_Owds[i];
    const auto& samples = window.owds;
    // 如果我自己是空窗口，那么就不统计
    if (samples.size() <= 0) {
      // 空窗口,不统计variance
      continue;
    }
    // 如果前一个窗口不为空,那么统计前一侧
    if (m_Owds[i - 1].owds.size() > 0) {
      flag = true;
      if (((m_Owds[i - 1].meanOwd > m_MeanOwd &&
            m_Owds[i].meanOwd < m_MeanOwd) ||
           (m_Owds[i - 1].meanOwd < m_MeanOwd &&
            m_Owds[i].meanOwd > m_MeanOwd)) &&
          (std::abs(m_Owds[i].meanOwd - m_MeanOwd) >= p_v * m_MeanVar)) {
        ans += 1;
      }
    }

    // 如果后一个窗口不为空，那么统计后一侧
    if (i + 1 < m_Owds.size() && m_Owds[i + 1].owds.size() > 0) {
      flag = true;
      if (((m_Owds[i + 1].meanOwd > m_MeanOwd &&
            m_Owds[i].meanOwd < m_MeanOwd) ||
           (m_Owds[i + 1].meanOwd < m_MeanOwd &&
            m_Owds[i].meanOwd > m_MeanOwd)) &&
          (std::abs(m_Owds[i].meanOwd - m_MeanOwd) >= p_v * m_MeanVar)) {
        ans += 1;
      }
    }
    n += flag ? 1 : 0;
  }
  if (n > 0)
    m_MeanFreq = ans / n;
  else
    m_MeanFreq = 0.0;
}

std::vector<double> MPTCP_SBD::CalGrowthSimilarity(MPTCP_SBD& sbd) {
  // 计算 OWD 增长趋势相似度
  std::vector<double> trends;
  std::vector<int> flags;  // 存储每个窗口的趋势标志：1 表示增长，-1 表示下降，0
                           // 表示无明显趋势
  for (size_t i = 1; i < sbd.m_Owds.size(); ++i) {
    auto& window_i = sbd.m_Owds[i];
    auto& window_i_1 = sbd.m_Owds[i - 1];
    auto& samples_i = window_i.owds;
    auto& samples_i_1 = window_i_1.owds;
    // 空窗口,不统计variance
    if (samples_i.size() <= 0 || samples_i_1.size() <= 0) {
      // 如果当前窗口或前一个窗口没有样本，趋势保持不变
      trends.push_back(trends.empty() ? 0 : trends.back());
      flags.push_back(trends.back() > 0 ? 1 : (trends.back() < 0 ? -1 : 0));
      // NS_LOG_INFO("case1 -Window [" << window_i_1.m_NowWindowEnd - 1000
      // << ",
      // "
      //                               << window_i_1.m_NowWindowEnd
      //                               << ") ms and Window ["
      //                               << window_i.m_NowWindowEnd - 1000 <<
      //                               ", "
      //                               << window_i.m_NowWindowEnd
      //                               << ") ms, mean OWD gap: " <<
      //                               trends.back());
      continue;
    }
    // 一个窗口内差值过大,直接赋值为0
    // if (window_i.max_Owd - window_i.min_Owd >
    //         10 * std::min(window_i.max_Owd, window_i.min_Owd) ||
    //     window_i_1.max_Owd - window_i_1.min_Owd >
    //         10 * std::min(window_i_1.max_Owd, window_i_1.min_Owd)) {
    //   trends.push_back(0);
    //   flags.push_back(0);
    //   continue;
    // }
    // // 如果两个窗口的平均 OWD 差值过大，跳过
    // if (std::abs(window_i.meanOwd - window_i_1.meanOwd) >=
    //     5 * std::min(window_i.meanOwd, window_i_1.meanOwd)) {
    //   trends.push_back(0);
    //   flags.push_back(0);
    //   // NS_LOG_INFO("case2 -Window ["
    //   //             << window_i_1.m_NowWindowEnd - 1000 << ", "
    //   //             << window_i_1.m_NowWindowEnd << ") ms and Window ["
    //   //             << window_i.m_NowWindowEnd - 1000 << ", "
    //   //             << window_i.m_NowWindowEnd << ") ms, mean OWD gap: 0");
    //   continue;
    // }
    double gap = window_i.meanOwd - window_i_1.meanOwd;
    // NS_LOG_INFO("case3 -Window ["
    //             << window_i_1.m_NowWindowEnd - 1000 << ", "
    //             << window_i_1.m_NowWindowEnd << ") ms and Window ["
    //             << window_i.m_NowWindowEnd - 1000 << ", "
    //             << window_i.m_NowWindowEnd << ") ms, mean OWD gap: " << gap);
    window_i.gap = gap;
    trends.push_back(gap);
    flags.push_back(trends.back() > 0 ? 1 : (trends.back() < 0 ? -1 : 0));
  }
  return trends;
}

double MPTCP_SBD::getSign(double x) {
  if (x > 0) return 1;
  if (x < 0) return -1;
  return 0;
}

// 计算 Pearson 相关系数
double MPTCP_SBD::PearsonCorrelation(const std::vector<double>& x,
                                     const std::vector<double>& y) {
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

// 计算 OWD 增长趋势的余弦相似度
double MPTCP_SBD::CosineSimilarity(const std::vector<double>& x,
                                   const std::vector<double>& y) {
  if (x.size() != y.size() || x.empty()) return 0.0;
  double dot = 0;
  double norm1 = 0;
  double norm2 = 0;

  for (size_t i = 0; i < x.size(); i++) {
    dot += getSign(x[i]) * getSign(y[i]);
    norm1 += getSign(x[i]) * getSign(x[i]);
    norm2 += getSign(y[i]) * getSign(y[i]);
  }

  double similarity = dot / (sqrt(norm1) * sqrt(norm2));
  return similarity;
}
void MPTCP_SBD::CalAll() {
  CalSkews();
  CalVars();
  CalFreqs();
}

std::pair<bool, bool>
MPTCP_SBD::isSharedBottleneck(MPTCP_SBD& a, MPTCP_SBD& b) {
  std::vector<double> a_g = CalGrowthSimilarity(a),
                      b_g = CalGrowthSimilarity(b);
  double similar_1 = PearsonCorrelation(a_g, b_g);
  double similar_2 = CosineSimilarity(a_g, b_g);
  a.m_GrowthOwdSimilarity_1 = similar_1;
  a.m_GrowthOwdSimilarity_2 = similar_2;
  b.m_GrowthOwdSimilarity_1 = similar_1;
  b.m_GrowthOwdSimilarity_2 = similar_2;
  bool res1 = true;
  bool res2 = false;
  // 第一步 是否拥塞
  bool a_flag1 = a.m_MeanSkew < c_s || (a.m_MeanSkew < c_h && a.PB);
  bool b_flag1 = b.m_MeanSkew < c_s || (b.m_MeanSkew < c_h && b.PB);
  if (a_flag1)
    a.SetPB(true);
  else
    a.SetPB(false);
  if (b_flag1)
    b.SetPB(true);
  else
    b.SetPB(false);

  // 2个流有1个不拥塞则不共享瓶颈
  if (!a_flag1 || !b_flag1) {
    res1 = false;
  }
  // 第二步 判断拥塞震荡周期是否相似
  if (std::abs(a.m_MeanFreq - b.m_MeanFreq) >= p_f) {
    res1 = false;
  }
  // 第三步 判断拥塞强度是否相似
  if (std::abs(a.m_MeanVar - b.m_MeanVar) >=
      p_mad * (std::max(a.m_MeanVar, b.m_MeanVar))) {
    res1 = false;
  }
  // 第四步 判断延迟是在“同时上升 / 同时下降/方向相反”
  if (std::abs(a.m_MeanSkew - b.m_MeanSkew) >= p_s) {
    res1 = false;
  }
  // 如果前面没有判断为共享瓶颈，同时2个流都没有因为缓冲区状况太好而停止下载,那么就判断增长趋势是否相似
  if (!a.isSleeping && !b.isSleeping) {
    // if (similar_1 > g_s || similar_2 > g_s) {
    //   res = true;
    // }
    if (similar_1 > g_s) {
      res2 = true;
    }
  }
  return std::make_pair(res1, res2);
}
double MPTCP_SBD::GetMeanSkew() const { return m_MeanSkew; }
double MPTCP_SBD::GetMeanVar() const { return m_MeanVar; }
double MPTCP_SBD::GetMeanFreq() const { return m_MeanFreq; }
std::pair<double, double> MPTCP_SBD::GetGrowthSimilarity() const {
  return std::make_pair(m_GrowthOwdSimilarity_1, m_GrowthOwdSimilarity_2);
}

}  // namespace ns3