#ifndef MPTCP_SBD_H
#define MPTCP_SBD_H

#include <cstdint>
#include <deque>
#include <vector>

namespace ns3 {

struct OwdInfo {
  std::vector<int64_t> owds;
  int64_t m_NowWindowEnd;
  double meanOwd;
  double min_Owd;
  double max_Owd;
  double gap;
};

class MPTCP_SBD {
 public:
  MPTCP_SBD() = default;
  // std::deque<std::pair<std::vector<int64_t>, int64_t>>
  //     m_Owds;                 // 存储最近的 OWD 样本（毫秒）
  std::deque<OwdInfo> m_Owds;  // 存储最近的 OWD 样本（毫秒）
  int64_t m_NowWindowEnd{0};   // 当前时间窗口结束时间
  bool isSleeping{false};      // 是否处于睡眠状态
  // 单窗口统计
  void AddMeanOwd();
  void DeleteMeanOwd();
  double GetMeanSkew() const;
  double GetMeanVar() const;
  double GetMeanFreq() const;
  std::pair<double, double> GetGrowthSimilarity() const;
  static std::pair<bool, bool> isSharedBottleneck(MPTCP_SBD& a, MPTCP_SBD& b);
  bool GetPB() const;
  void SetPB(bool value);

 private:
  void CalSkews();
  void CalVars();
  void CalFreqs();
  void CalAll();
  static double PearsonCorrelation(const std::vector<double>& x,
                                   const std::vector<double>& y);
  static double
  CosineSimilarity(const std::vector<double>& x, const std::vector<double>& y);
  static std::vector<double> CalGrowthSimilarity(MPTCP_SBD& sbd);
  static double getSign(double x);
  bool PB = false;
  // int64_t
  std::deque<double> m_BaseMeanOwds;  // 每个时间窗口的平均 OWD
  double m_MeanOwd;                   // 所有时间窗口的平均 OWD

  double m_MeanSkew;  // 所有时间窗口的平均 Skewness

  double m_MeanVar;  // 所有时间窗口的平均 Variance

  double m_MeanFreq;  // 所有时间窗口的平均 Frequency

  double m_GrowthOwdSimilarity_1;  // OWD 增长趋势皮尔森相似度
  double m_GrowthOwdSimilarity_2;  // OWD 增长趋势余弦相似度
  // 参数

  // static constexpr double c_s = -0.01;
  static constexpr double c_s = 0.3;  // 更加宽松
  // static constexpr double c_h = 0.3;
  static constexpr double c_h = 0.5;  // 更加宽松
  // static constexpr double p_f = 0.1;
  static constexpr double p_f = 0.35;  // 更加宽松
  // static constexpr double p_s = 0.1;
  static constexpr double p_s = 0.2;  // 更加宽松
  // static constexpr double p_v = 0.7;
  static constexpr double p_v = 0.5;  // 更加宽松
  // static constexpr double p_mad = 0.1;
  static constexpr double p_mad = 0.3;  // 更加宽松

  // 新添的参数 （增长趋势相似度判断）
  static constexpr double g_s = 0.65;
};

}  // namespace ns3
#endif
