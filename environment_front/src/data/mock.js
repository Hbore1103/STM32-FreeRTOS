/**
 * 环境监测 mock 数据模块
 * 提供指标定义、实时数据生成、历史趋势、告警记录的模拟
 */

// ========== 可控制开关（仅 mock 模式）==========
export const MOCK_CONTROLS = [
  {
    key: "fan",
    name: "风扇",
    icon: "🌀",
    unit: "",
    effect: { temperature: -1.5, co2: -30 },
  },
  {
    key: "humidifier",
    name: "加湿器",
    icon: "💧",
    unit: "",
    effect: { humidity: +8 },
  },
  { key: "beep", name: "蜂鸣器", icon: "🔔", unit: "", effect: {} },
  {
    key: "ac",
    name: "空调",
    icon: "❄️",
    unit: "",
    effect: { temperature: -2.5, humidity: -3, co2: -50 },
  },
];

// 开关当前状态（全局共享，useEnvData 读取）
export const controlState = {
  fan: 0,
  humidifier: 0,
  beep: 0,
  ac: 0,
};

// 设置开关状态，返回新状态
export function setControl(key, value) {
  if (key in controlState) {
    controlState[key] = value ? 1 : 0;
  }
  return controlState;
}

// 计算开关对各项指标的修正量
function controlAdjustment() {
  const adj = {};
  for (const c of MOCK_CONTROLS) {
    if (controlState[c.key] && c.effect) {
      for (const [k, v] of Object.entries(c.effect)) {
        adj[k] = (adj[k] || 0) + v;
      }
    }
  }
  return adj;
}

// 指标定义：阈值用于判定等级与告警
export const METRICS = [
  {
    key: "pm25",
    name: "PM2.5",
    unit: "μg/m³",
    icon: "🌫",
    levels: [
      { max: 35, label: "优", color: "#10b981" },
      { max: 75, label: "良", color: "#84cc16" },
      { max: 115, label: "轻度污染", color: "#f59e0b" },
      { max: 150, label: "中度污染", color: "#f97316" },
      { max: 250, label: "重度污染", color: "#ef4444" },
      { max: Infinity, label: "严重污染", color: "#991b1b" },
    ],
    warn: 75,
    danger: 150,
  },
  {
    key: "pm10",
    name: "PM10",
    unit: "μg/m³",
    icon: "🌫",
    levels: [
      { max: 50, label: "优", color: "#10b981" },
      { max: 150, label: "良", color: "#84cc16" },
      { max: 250, label: "轻度污染", color: "#f59e0b" },
      { max: 350, label: "中度污染", color: "#f97316" },
      { max: 420, label: "重度污染", color: "#ef4444" },
      { max: Infinity, label: "严重污染", color: "#991b1b" },
    ],
    warn: 150,
    danger: 350,
  },
  {
    key: "temperature",
    name: "温度",
    unit: "°C",
    icon: "🌡",
    levels: [
      { max: 10, label: "偏冷", color: "#3b82f6" },
      { max: 18, label: "凉爽", color: "#06b6d4" },
      { max: 26, label: "舒适", color: "#10b981" },
      { max: 32, label: "偏热", color: "#f59e0b" },
      { max: Infinity, label: "炎热", color: "#ef4444" },
    ],
    warn: 32,
    danger: 38,
  },
  {
    key: "humidity",
    name: "湿度",
    unit: "%",
    icon: "💧",
    levels: [
      { max: 30, label: "干燥", color: "#f59e0b" },
      { max: 60, label: "舒适", color: "#10b981" },
      { max: 80, label: "偏湿", color: "#06b6d4" },
      { max: Infinity, label: "潮湿", color: "#3b82f6" },
    ],
    warn: 80,
    danger: 90,
  },
  {
    key: "co2",
    name: "CO₂",
    unit: "ppm",
    icon: "🫧",
    levels: [
      { max: 800, label: "良好", color: "#10b981" },
      { max: 1000, label: "一般", color: "#84cc16" },
      { max: 1500, label: "偏高", color: "#f59e0b" },
      { max: Infinity, label: "超标", color: "#ef4444" },
    ],
    warn: 1000,
    danger: 1500,
  },
  {
    key: "noise",
    name: "噪声",
    unit: "dB",
    icon: "🔊",
    levels: [
      { max: 40, label: "安静", color: "#10b981" },
      { max: 60, label: "正常", color: "#84cc16" },
      { max: 75, label: "偏吵", color: "#f59e0b" },
      { max: Infinity, label: "吵闹", color: "#ef4444" },
    ],
    warn: 75,
    danger: 85,
  },
];

// 监测站点
export const STATIONS = [
  { id: "S001", name: "1号监测站", location: "厂区主入口" },
  { id: "S002", name: "2号监测站", location: "生产车间 A" },
  { id: "S003", name: "3号监测站", location: "生产车间 B" },
  { id: "S004", name: "4号监测站", location: "办公区域" },
];

// 基准值，用于随机波动的中心
const BASELINE = {
  pm25: 48,
  pm10: 95,
  temperature: 24,
  humidity: 58,
  co2: 720,
  noise: 52,
};

// 工具：生成 [min, max] 范围的随机数，保留一位小数
function randomAround(base, delta) {
  const v = base + (Math.random() - 0.5) * 2 * delta;
  return Math.round(v * 10) / 10;
}

// 根据 metric key 与值返回对应等级
export function getLevel(metricKey, value) {
  const metric = METRICS.find((m) => m.key === metricKey);
  if (!metric) return null;
  return metric.levels.find((lv) => value <= lv.max);
}

// 生成一组当前指标值
export function generateSnapshot() {
  const snapshot = {};
  const adj = controlAdjustment();
  for (const m of METRICS) {
    // 大部分时间贴近基准，偶尔拉高模拟波动
    const spike = Math.random() < 0.15;
    const base = spike ? BASELINE[m.key] * 1.4 : BASELINE[m.key];
    const delta = base * 0.12;
    let v = randomAround(base, delta);
    // 叠加开关影响
    if (adj[m.key]) v = v + adj[m.key];
    // 保证浓度类指标不为负
    if (v < 0 && ["pm25", "pm10", "co2", "noise"].includes(m.key)) v = 0;
    snapshot[m.key] = Math.round(v * 10) / 10;
  }
  return snapshot;
}

// 生成最近 N 个时间点的趋势数据
export function generateHistory(points = 24) {
  const now = Date.now();
  const step = 5 * 60 * 1000; // 5 分钟一个点
  const series = {};
  METRICS.forEach((m) => (series[m.key] = []));

  for (let i = points - 1; i >= 0; i--) {
    const t = now - i * step;
    const snap = generateSnapshot();
    METRICS.forEach((m) => {
      series[m.key].push({ time: t, value: snap[m.key] });
    });
  }
  return series;
}

// 告警类型映射
export const ALARM_TYPES = {
  pm25: "PM2.5 超标",
  pm10: "PM10 超标",
  temperature: "温度异常",
  humidity: "湿度异常",
  co2: "CO₂ 浓度超标",
  noise: "噪声超标",
};

// 生成一批历史告警
export function generateAlarms(count = 30) {
  const alarms = [];
  const now = Date.now();
  const statuses = [
    { key: "pending", label: "待处理", color: "#f59e0b" },
    { key: "processing", label: "处理中", color: "#3b82f6" },
    { key: "resolved", label: "已解决", color: "#10b981" },
    { key: "ignored", label: "已忽略", color: "#9ca3af" },
  ];
  // 让"已解决"占多数
  const weighted = [
    "resolved",
    "resolved",
    "resolved",
    "processing",
    "pending",
    "ignored",
  ];

  for (let i = 0; i < count; i++) {
    const metric = METRICS[Math.floor(Math.random() * METRICS.length)];
    const station = STATIONS[Math.floor(Math.random() * STATIONS.length)];
    const statusKey = weighted[Math.floor(Math.random() * weighted.length)];
    const status = statuses.find((s) => s.key === statusKey);
    const value =
      Math.round(
        (metric.warn + Math.random() * (metric.danger - metric.warn) * 1.5) *
          10,
      ) / 10;
    const level = value >= metric.danger ? "danger" : "warning";
    const occur = now - Math.floor(Math.random() * 72) * 3600 * 1000;

    alarms.push({
      id: `A${String(10000 + i).padStart(5, "0")}`,
      stationId: station.id,
      stationName: station.name,
      stationLocation: station.location,
      metricKey: metric.key,
      metricName: metric.name,
      alarmType: ALARM_TYPES[metric.key],
      value,
      threshold: level === "danger" ? metric.danger : metric.warn,
      unit: metric.unit,
      level, // warning | danger
      status: status.key,
      statusLabel: status.label,
      statusColor: status.color,
      occurTime: occur,
      resolveTime:
        statusKey === "resolved"
          ? occur + Math.floor(Math.random() * 3600 * 1000)
          : null,
    });
  }
  // 按时间倒序
  alarms.sort((a, b) => b.occurTime - a.occurTime);
  return alarms;
}
