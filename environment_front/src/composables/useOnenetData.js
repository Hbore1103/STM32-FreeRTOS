/**
 * OneNET 数据源 composable
 * 从后端代理 /api/onenet/* 获取物模型定义、最新属性、历史数据
 * 动态根据物模型生成指标列表，不再硬编码
 *
 * 用法:
 *   const { metrics, current, history, thingModel, status, error, refresh } = useOnenetData(deviceName)
 *
 *   metrics: 由物模型动态生成的指标数组，含 level/unit/icon 等
 *   current: { identifier: value, ... }
 *   history: { identifier: [{time,value}, ...], ... }
 */
import { ref, computed, onMounted, onUnmounted } from "vue";

const REFRESH_INTERVAL = 10000; // 10 秒拉一次最新数据

async function fetchJson(url) {
  const res = await fetch(url);
  if (!res.ok) {
    const txt = await res.text().catch(() => "");
    throw new Error(`${res.status} ${txt || res.statusText}`);
  }
  const body = await res.json();
  if (body.code !== 0) throw new Error(body.msg || "接口错误");
  return body.data;
}

/**
 * 根据 identifier 推断默认单位（OneNET 物模型 specs.unit 通常为空）
 */
function inferUnit(identifier, dataType) {
  const id = String(identifier).toLowerCase();
  if (id.includes("temp")) return "°C";
  if (id.includes("humi")) return "%";
  if (id.includes("pm2")) return "μg/m³";
  if (id.includes("pm10")) return "μg/m³";
  if (id.includes("co2")) return "ppm";
  if (id.includes("noise") || id.includes("sound")) return "dB";
  if (id.includes("beep") || id.includes("fan") || id.includes("servo"))
    return "";
  // bool/enum 类型通常是开关状态
  if (dataType === "bool") return "";
  if (dataType === "enum") return "";
  return "";
}

/**
 * 给有意义的监测指标定义等级阈值（用于颜色标识）
 * 其他属性（如开关类）返回 null 表示不分级
 */
function getLevels(identifier) {
  const id = String(identifier).toLowerCase();
  if (id.includes("temp")) {
    return [
      { max: 10, label: "偏冷", color: "#3b82f6" },
      { max: 18, label: "凉爽", color: "#06b6d4" },
      { max: 26, label: "舒适", color: "#10b981" },
      { max: 32, label: "偏热", color: "#f59e0b" },
      { max: Infinity, label: "炎热", color: "#ef4444" },
    ];
  }
  if (id.includes("humi")) {
    return [
      { max: 30, label: "干燥", color: "#f59e0b" },
      { max: 60, label: "舒适", color: "#10b981" },
      { max: 80, label: "偏湿", color: "#06b6d4" },
      { max: Infinity, label: "潮湿", color: "#3b82f6" },
    ];
  }
  // PM2.5 / CO2 / 噪声 等也可以按需扩展
  return null;
}

function getLevelForValue(levels, value) {
  if (!levels) return null;
  return levels.find((lv) => value <= lv.max) || levels[levels.length - 1];
}

/**
 * 根据物模型属性生成指标定义数组
 */
function buildMetricsFromThingModel(properties) {
  return properties.map((p) => {
    const dataType = p.dataType?.type || "int32";
    const unit = p.dataType?.specs?.unit || inferUnit(p.identifier, dataType);
    const levels = getLevels(p.identifier);
    return {
      key: p.identifier, // 直接用 identifier 作为 key，不再需要映射
      identifier: p.identifier,
      name: p.name || p.identifier,
      unit,
      icon: pickIcon(p.identifier),
      dataType,
      accessMode: p.accessMode,
      levels,
      warn: levels ? levels[2]?.max : null,
      danger: levels ? levels[3]?.max : null,
    };
  });
}

function pickIcon(identifier) {
  const id = String(identifier).toLowerCase();
  if (id.includes("temp")) return "🌡";
  if (id.includes("humi")) return "💧";
  if (id.includes("pm")) return "🌫";
  if (id.includes("co2")) return "🫧";
  if (id.includes("noise") || id.includes("sound")) return "🔊";
  if (id.includes("beep")) return "🔔";
  if (id.includes("fan")) return "🌀";
  if (id.includes("servo")) return "⚙️";
  if (id.includes("light")) return "💡";
  if (id.includes("switch")) return "🔘";
  return "📊";
}

/**
 * @param {import('vue').Ref<string>|string} deviceName
 */
export function useOnenetData(deviceName = "") {
  // metrics 从物模型动态生成，初始为空，loadThingModel 后填充
  const metrics = ref([]);
  const thingModel = ref(null);

  const current = ref({}); // { identifier: number, ... }
  const history = ref({}); // { identifier: [{time,value}, ...], ... }
  const lastUpdate = ref(null);
  const status = ref("idle"); // idle | loading | ok | error
  const error = ref(null);
  const deviceLastSeen = ref(null); // OneNET 上设备最新数据的时间戳（毫秒）
  const DEVICE_ONLINE_THRESHOLD = 60000; // 60 秒内有数据视为在线
  const deviceOnline = computed(
    () =>
      deviceLastSeen.value != null &&
      Date.now() - deviceLastSeen.value < DEVICE_ONLINE_THRESHOLD,
  );
  let timer = null;

  const getDevice = () =>
    typeof deviceName === "string" ? deviceName : deviceName.value;

  async function loadThingModel() {
    try {
      const data = await fetchJson("/api/onenet/thingmodel");
      thingModel.value = data;
      const props = data.properties || [];
      metrics.value = buildMetricsFromThingModel(props);
      return props;
    } catch (e) {
      console.warn("[thingmodel] load failed:", e.message);
      throw e;
    }
  }

  async function loadProperty() {
    const dn = getDevice();
    if (!dn) {
      status.value = "error";
      error.value = "未配置设备名";
      return;
    }
    status.value = "loading";
    try {
      const data = await fetchJson(
        `/api/onenet/property/${encodeURIComponent(dn)}`,
      );
      const list = Array.isArray(data) ? data : data?.list || [];

      const snap = {};
      let maxTime = 0;
      for (const item of list) {
        const num = Number(item.value);
        snap[item.identifier] = Number.isNaN(num) ? item.value : num;
        if (item.time) {
          const t = Number(item.time);
          if (!Number.isNaN(t) && t > maxTime) maxTime = t;
        }
      }
      current.value = snap;
      lastUpdate.value = Date.now();
      if (maxTime > 0) deviceLastSeen.value = maxTime;
      status.value = "ok";
      error.value = null;
      checkOnenetAlarms(snap, dn);
    } catch (e) {
      status.value = "error";
      error.value = e.message;
    }
  }

  async function loadHistory(identifier, hours = 2) {
    const dn = getDevice();
    if (!dn) return;
    try {
      const data = await fetchJson(
        `/api/onenet/history/${encodeURIComponent(dn)}/${encodeURIComponent(identifier)}?hours=${hours}`,
      );
      const list = Array.isArray(data) ? data : data?.list || [];
      const points = list
        .map((p) => ({ time: Number(p.time), value: Number(p.value) }))
        .sort((a, b) => a.time - b.time);
      history.value = { ...history.value, [identifier]: points };
    } catch (e) {
      console.warn("[history] load failed:", e.message);
    }
  }

  async function refresh() {
    await loadProperty();
    if (status.value === "ok") {
      // 为每个有数值的属性加载历史
      for (const m of metrics.value) {
        await loadHistory(m.identifier);
      }
    }
  }

  // 远程控制：设置设备属性
  // params: { Fan: 1 } 或 { Beep: 0 }
  async function setProperty(params) {
    const dn = getDevice();
    if (!dn) throw new Error("未配置设备名");
    const res = await fetch(`/api/onenet/set/${encodeURIComponent(dn)}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(params),
    });
    const body = await res.json();
    if (body.code !== 0) throw new Error(body.msg || "控制命令失败");
    // 成功后立即刷新
    await loadProperty();
    return body.data;
  }

  // 返回可控制的指标（accessMode 包含 'w' 的）
  const controllableMetrics = computed(() =>
    metrics.value.filter((m) => String(m.accessMode || "").includes("w")),
  );

  // OneNET 告警列表
  const alarms = ref([]);
  let onenetAlarmCounter = 20000;

  function pushAlarm(metric, value, level, deviceName) {
    // 同类型未解决告警不重复生成
    const lastSame = alarms.value.find(
      (a) =>
        a.metricKey === metric.identifier &&
        (a.status === "pending" || a.status === "processing"),
    );
    if (lastSame) return;
    alarms.value = [
      {
        id: `A${String(onenetAlarmCounter++).padStart(5, "0")}`,
        stationId: deviceName,
        stationName: deviceName,
        stationLocation: "OneNET 云平台",
        metricKey: metric.identifier,
        metricName: metric.name,
        alarmType: `${metric.name}异常`,
        value: Math.round(value * 10) / 10,
        threshold: level === "danger" ? 35 : 32, // OneNET 没有 warn/danger 配置，用温度示例
        unit: metric.unit || "",
        level,
        status: "pending",
        statusLabel: "待处理",
        statusColor: "#f59e0b",
        occurTime: Date.now(),
        resolveTime: null,
      },
      ...alarms.value,
    ].slice(0, 200);
  }

  function checkOnenetAlarms(snap, deviceName) {
    for (const m of metrics.value) {
      const v = snap[m.identifier];
      if (v === undefined || v === null || typeof v !== "number") continue;
      // 用 levels 的最后两个阈值作为 warn/danger
      if (!m.levels || m.levels.length < 3) continue;
      const warnLv = m.levels[m.levels.length - 3]; // 倒数第三级 ≈ warn
      const dangerLv = m.levels[m.levels.length - 1]; // 最后一级 ≈ danger
      if (dangerLv && v > dangerLv.max) {
        pushAlarm(m, v, "danger", deviceName);
      } else if (warnLv && v > warnLv.max) {
        pushAlarm(m, v, "warning", deviceName);
      }
    }
    // 告警状态自动流转
    const now = Date.now();
    alarms.value = alarms.value.map((a) => {
      if (a.status === "pending" && now - a.occurTime > 20000) {
        return {
          ...a,
          status: "processing",
          statusLabel: "处理中",
          statusColor: "#3b82f6",
        };
      }
      if (a.status === "processing" && now - a.occurTime > 60000) {
        return {
          ...a,
          status: "resolved",
          statusLabel: "已解决",
          statusColor: "#10b981",
          resolveTime: now,
        };
      }
      return a;
    });
  }

  onMounted(async () => {
    try {
      await loadThingModel();
    } catch (e) {
      // 物模型加载失败不致命，继续尝试拉数据
      console.warn("[onenet] thingmodel fail, continue");
    }
    if (getDevice()) {
      await loadProperty();
    }
    timer = setInterval(loadProperty, REFRESH_INTERVAL);
  });

  onUnmounted(() => {
    if (timer) clearInterval(timer);
  });

  return {
    metrics,
    controllableMetrics,
    alarms,
    thingModel,
    current,
    history,
    lastUpdate,
    status,
    error,
    deviceOnline,
    deviceLastSeen,
    refresh,
    loadHistory,
    setProperty,
    getLevelForValue,
  };
}
