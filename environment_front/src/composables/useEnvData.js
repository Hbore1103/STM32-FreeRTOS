/**
 * 实时数据 composable：维护响应式的当前指标快照与历史趋势
 * 通过 setInterval 模拟数据实时刷新
 * 同时根据阈值自动生成告警记录
 */
import { ref, onMounted, onUnmounted } from "vue";
import {
  METRICS,
  STATIONS,
  ALARM_TYPES,
  generateSnapshot,
  generateHistory,
  generateAlarms,
} from "../data/mock.js";

const REFRESH_INTERVAL = 5000; // 5s 刷新一次

let alarmCounter = 10000;

function buildAlarm(metricKey, value, level, stationIndex = 0) {
  const metric = METRICS.find((m) => m.key === metricKey);
  const station = STATIONS[stationIndex % STATIONS.length];
  const threshold = level === "danger" ? metric.danger : metric.warn;
  return {
    id: `A${String(alarmCounter++).padStart(5, "0")}`,
    stationId: station.id,
    stationName: station.name,
    stationLocation: station.location,
    metricKey: metric.key,
    metricName: metric.name,
    alarmType: ALARM_TYPES[metric.key] || `${metric.name}异常`,
    value: Math.round(value * 10) / 10,
    threshold,
    unit: metric.unit,
    level, // warning | danger
    status: "pending",
    statusLabel: "待处理",
    statusColor: "#f59e0b",
    occurTime: Date.now(),
    resolveTime: null,
  };
}

// 检测当前值是否触发新告警，返回新增告警数组
function checkThresholds(snapshot) {
  const newAlarms = [];
  for (const m of METRICS) {
    const v = snapshot[m.key];
    if (v === undefined || v === null) continue;
    let level = null;
    if (m.danger !== undefined && v >= m.danger) level = "danger";
    else if (m.warn !== undefined && v >= m.warn) level = "warning";
    if (level) {
      // 只在没有同类型未解决告警时才生成新告警（避免刷屏）
      const lastSame = alarms.value.find(
        (a) =>
          a.metricKey === m.key &&
          (a.status === "pending" || a.status === "processing"),
      );
      if (!lastSame) {
        newAlarms.push(buildAlarm(m.key, v, level));
      }
    }
  }
  return newAlarms;
}

// 模拟告警流转：pending → processing → resolved/ignored
function ageAlarms() {
  const now = Date.now();
  alarms.value = alarms.value.map((a) => {
    if (a.status === "pending" && now - a.occurTime > 15000) {
      return {
        ...a,
        status: "processing",
        statusLabel: "处理中",
        statusColor: "#3b82f6",
      };
    }
    if (a.status === "processing" && now - a.occurTime > 40000) {
      return {
        ...a,
        status: "resolved",
        statusLabel: "已解决",
        statusColor: "#10b981",
        resolveTime: a.occurTime + 30000 + Math.random() * 10000,
      };
    }
    return a;
  });
}

export function useEnvData() {
  const metrics = METRICS;
  const current = ref(generateSnapshot());
  const history = ref(generateHistory(24));
  const alarms = ref(generateAlarms(30));
  const lastUpdate = ref(Date.now());
  const active = ref(false);

  let timer = null;

  function tick() {
    if (!active.value) return;
    const snap = generateSnapshot();
    current.value = snap;

    const now = Date.now();
    METRICS.forEach((m) => {
      const arr = history.value[m.key];
      arr.push({ time: now, value: snap[m.key] });
      if (arr.length > 24) arr.shift();
    });
    history.value = { ...history.value };
    lastUpdate.value = now;

    // 阈值检测 + 告警流转
    const newAlarms = checkThresholds(snap);
    if (newAlarms.length) {
      alarms.value = [...newAlarms, ...alarms.value].slice(0, 200);
    }
    ageAlarms();
  }

  function pause() {
    active.value = false;
  }
  function resume() {
    active.value = true;
  }
  function toggle() {
    active.value = !active.value;
  }

  onMounted(() => {
    timer = setInterval(tick, REFRESH_INTERVAL);
  });
  onUnmounted(() => {
    if (timer) clearInterval(timer);
  });

  return {
    metrics,
    current,
    history,
    alarms,
    lastUpdate,
    active,
    pause,
    resume,
    toggle,
  };
}
