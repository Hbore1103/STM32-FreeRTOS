<script setup>
import {
  ref,
  reactive,
  computed,
  watch,
  onMounted,
  onUnmounted,
  nextTick,
  inject,
} from "vue";
import * as echarts from "echarts/core";
import { LineChart } from "echarts/charts";
import {
  GridComponent,
  TooltipComponent,
  LegendComponent,
} from "echarts/components";
import { CanvasRenderer } from "echarts/renderers";
import { useEnvData } from "../composables/useEnvData.js";
import { useOnenetData } from "../composables/useOnenetData.js";
import {
  getLevel,
  MOCK_CONTROLS,
  controlState,
  setControl,
  generateSnapshot,
  METRICS,
} from "../data/mock.js";

echarts.use([
  LineChart,
  GridComponent,
  TooltipComponent,
  LegendComponent,
  CanvasRenderer,
]);

// 数据源切换：mock | onenet（从 App.vue 注入）
const dataSource = inject("dataSource");
const onenetDevice = ref("");
const envData = useEnvData();
const onenet = useOnenetData(onenetDevice);

// mock 开关状态（用 reactive 让模板能响应式读取）
const mockSwitchState = reactive({ ...controlState });

// mock 模式切换开关
function toggleMock(key, value) {
  setControl(key, value);
  mockSwitchState[key] = value;
  // 立即刷新一次数据让效果可见
  envData.current.value = generateSnapshot();
  const now = Date.now();
  METRICS.forEach((m) => {
    const arr = envData.history.value[m.key];
    arr.push({ time: now, value: envData.current.value[m.key] });
    if (arr.length > 24) arr.shift();
  });
  envData.history.value = { ...envData.history.value };
  envData.lastUpdate.value = now;
  controlMsg.value = value
    ? `已开启 ${MOCK_CONTROLS.find((c) => c.key === key)?.name}`
    : `已关闭 ${MOCK_CONTROLS.find((c) => c.key === key)?.name}`;
  setTimeout(() => (controlMsg.value = null), 2000);
}

// 开关效果标签
const EFFECT_NAMES = {
  temperature: "温度",
  humidity: "湿度",
  co2: "CO₂",
  pm25: "PM2.5",
  pm10: "PM10",
  noise: "噪声",
};
function effectLabel(effect) {
  const parts = Object.entries(effect).map(([k, v]) => {
    const sign = v > 0 ? "+" : "";
    return `${EFFECT_NAMES[k] || k}${sign}${v}`;
  });
  return `效果: ${parts.join(", ")}`;
}

// 控制面板状态
const controlLoading = ref(false);
const controlMsg = ref(null);

// 发送控制命令
async function sendControl(metric, value) {
  if (controlLoading.value) return;
  controlLoading.value = true;
  controlMsg.value = null;
  try {
    await onenet.setProperty({ [metric.identifier]: value });
    controlMsg.value = `成功: ${metric.name} → ${value}`;
  } catch (e) {
    controlMsg.value = `失败: ${e.message}`;
  } finally {
    controlLoading.value = false;
    setTimeout(() => (controlMsg.value = null), 3000);
  }
}

// AI 对话
const aiMessages = ref([]); // { role: 'user'|'assistant', content, error? }
const aiInput = ref("");
const aiLoading = ref(false);
const chatBodyRef = ref(null); // 用于滚动到底

function currentEnvPayload() {
  const tempMetric = metrics.value.find(
    (m) => m.key.toLowerCase().includes("temp") || m.name.includes("温度"),
  );
  const humMetric = metrics.value.find(
    (m) => m.key.toLowerCase().includes("humi") || m.name.includes("湿度"),
  );
  const temperature = tempMetric ? current.value[tempMetric.key] : undefined;
  const humidity = humMetric ? current.value[humMetric.key] : undefined;
  // 把其他指标也带上
  const others = metrics.value
    .filter((m) => m !== tempMetric && m !== humMetric)
    .map((m) => ({ name: m.name, value: current.value[m.key], unit: m.unit }));
  return { temperature, humidity, metrics: others };
}

// 可控设备列表（OneNET 模式取 controllableMetrics，mock 模式取虚拟开关）
function controllablePayload() {
  if (dataSource.value === "onenet") {
    return (onenet.controllableMetrics.value || []).map((m) => ({
      identifier: m.identifier,
      name: m.name,
      values: [0, 1],
    }));
  }
  // mock 模式：用首字母大写作为 identifier（跟 few-shot 示例一致），
  // handleAiDeviceChanges 会做大小写不敏感匹配
  return MOCK_CONTROLS.map((c) => ({
    identifier: c.key.charAt(0).toUpperCase() + c.key.slice(1),
    name: c.name,
    values: [0, 1],
  }));
}

function deviceNameForEnv() {
  if (dataSource.value === "onenet") return onenetDevice.value || "";
  return "mock-device";
}

function scrollChatToBottom() {
  nextTick(() => {
    if (chatBodyRef.value)
      chatBodyRef.value.scrollTop = chatBodyRef.value.scrollHeight;
  });
}

async function sendChat(text) {
  const content = (text ?? aiInput.value).trim();
  if (!content || aiLoading.value) return;
  aiInput.value = "";
  aiMessages.value.push({ role: "user", content });
  aiLoading.value = true;
  scrollChatToBottom();
  try {
    const env = {
      ...currentEnvPayload(),
      controllable: controllablePayload(),
      deviceName: deviceNameForEnv(),
    };
    const res = await fetch("/api/ai/chat", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        messages: aiMessages.value.map((m) => ({
          role: m.role,
          content: m.content,
        })),
        env,
      }),
    });
    const body = await res.json();
    if (body.code !== 0) throw new Error(body.msg || "AI 回复失败");
    const reply = {
      role: "assistant",
      content: body.data.content,
      deviceChanges: body.data.deviceChanges || [],
    };
    aiMessages.value.push(reply);
    // 如果有设备变更，触发本地数据刷新
    if (reply.deviceChanges.length) {
      handleAiDeviceChanges(reply.deviceChanges);
    }
  } catch (e) {
    aiMessages.value.push({ role: "assistant", content: "", error: e.message });
  } finally {
    aiLoading.value = false;
    scrollChatToBottom();
  }
}

// 处理 AI 触发的设备变更
function handleAiDeviceChanges(changes) {
  if (dataSource.value === "onenet") {
    // OneNET 模式：3 秒后刷新属性（让设备有时间响应）
    setTimeout(() => onenet.refresh(), 3000);
  } else {
    // mock 模式：复用 toggleMock 让 UI 状态和数据都更新
    for (const c of changes) {
      // 大小写不敏感匹配
      const target = MOCK_CONTROLS.find(
        (m) => m.key === c.identifier || m.key === c.identifier.toLowerCase(),
      );
      if (target) {
        toggleMock(target.key, c.value);
      }
    }
  }
}

// 一键环境分析：发送预设 prompt
function quickAnalyze() {
  sendChat("请根据当前传感器数据给出环境评估、健康影响和建议措施。");
}

function clearChat() {
  aiMessages.value = [];
}

// 兼容旧变量名（避免破坏其他引用）
const aiContent = computed(
  () =>
    aiMessages.value.filter((m) => m.role === "assistant").pop()?.content || "",
);
const aiError = computed(
  () => aiMessages.value.filter((m) => m.error).pop()?.error || "",
);

function onAiKeydown(e) {
  if (e.key === "Enter" && !e.shiftKey) {
    e.preventDefault();
    sendChat();
  }
}

// metrics 按数据源取：OneNET 动态生成，mock 硬编码
const metrics = computed(() =>
  dataSource.value === "onenet" ? onenet.metrics.value : envData.metrics,
);

const current = computed(() =>
  dataSource.value === "onenet" ? onenet.current.value : envData.current.value,
);
const history = computed(() =>
  dataSource.value === "onenet" ? onenet.history.value : envData.history.value,
);
const lastUpdate = computed(() =>
  dataSource.value === "onenet"
    ? onenet.lastUpdate.value
    : envData.lastUpdate.value,
);

// 选中的趋势指标
const selectedKey = ref(null);
watch(
  metrics,
  (list) => {
    // 切换数据源或物模型加载后，默认选中第一个
    if (list.length && !list.find((m) => m.key === selectedKey.value)) {
      selectedKey.value = list[0].key;
    }
  },
  { immediate: true },
);

const selectedMetric = computed(() =>
  metrics.value.find((m) => m.key === selectedKey.value),
);

// 指标卡片数据
const cards = computed(() =>
  metrics.value.map((m) => {
    const value = current.value[m.key] ?? 0;
    // OneNET 模式用动态等级，mock 模式用硬编码等级
    const level =
      dataSource.value === "onenet"
        ? onenet.getLevelForValue(m.levels, value)
        : getLevel(m.key, value);
    return { ...m, value, level };
  }),
);

// 时间格式化
function formatTime(ts) {
  const d = new Date(ts);
  const hh = String(d.getHours()).padStart(2, "0");
  const mm = String(d.getMinutes()).padStart(2, "0");
  return `${hh}:${mm}`;
}
function formatFullTime(ts) {
  const d = new Date(ts);
  return `${d.getMonth() + 1}-${d.getDate()} ${formatTime(ts)}:${String(d.getSeconds()).padStart(2, "0")}`;
}

// 设备最后上报时间的相对描述
function formatLastSeen(ts) {
  if (!ts) return "-";
  const diff = (Date.now() - ts) / 1000; // 秒
  if (diff < 60) return `${Math.floor(diff)} 秒前`;
  if (diff < 3600) return `${Math.floor(diff / 60)} 分钟前`;
  if (diff < 86400) return `${Math.floor(diff / 3600)} 小时前`;
  return `${Math.floor(diff / 86400)} 天前`;
}

const deviceOnlineText = computed(() => {
  if (onenet.deviceOnline.value === true) return "设备在线";
  if (onenet.deviceLastSeen.value != null)
    return `设备离线 · 最后上报 ${formatLastSeen(onenet.deviceLastSeen.value)}`;
  if (onenet.status.value === "loading") return "检测中…";
  return "设备状态未知";
});

// 切换到 OneNET：若未填设备名则尝试用后端默认配置
async function switchOnenet() {
  dataSource.value = "onenet";
  // 若未填，尝试从 /api/health 拿默认设备名
  if (!onenetDevice.value) {
    try {
      const res = await fetch("/api/health");
      const body = await res.json();
      if (body?.data?.defaultDevice)
        onenetDevice.value = body.data.defaultDevice;
    } catch (e) {
      // ignore
    }
  }
  await onenet.refresh();
}

const onenetStatusText = computed(() => {
  const s = onenet.status.value;
  const map = {
    idle: "未加载",
    loading: "加载中…",
    ok: "已连接",
    error: "错误",
  };
  return map[s] || s;
});

// ECharts 实例
const chartEl = ref(null);
let chart = null;

function buildOption() {
  const m = selectedMetric.value;
  const series = history.value[m.key] || [];
  const xs = series.map((p) => formatTime(p.time));
  const ys = series.map((p) => p.value);

  return {
    grid: { left: 48, right: 24, top: 24, bottom: 36 },
    tooltip: {
      trigger: "axis",
      formatter: (params) => {
        const p = params[0];
        return `${p.axisValue}<br/>${m.name}: <b>${p.value}</b> ${m.unit}`;
      },
    },
    xAxis: {
      type: "category",
      data: xs,
      axisLine: { lineStyle: { color: "#e5e7eb" } },
      axisLabel: { color: "#6b7280", fontSize: 11 },
    },
    yAxis: {
      type: "value",
      axisLine: { show: false },
      axisTick: { show: false },
      axisLabel: { color: "#6b7280", fontSize: 11 },
      splitLine: { lineStyle: { color: "#f3f4f6" } },
    },
    series: [
      {
        name: m.name,
        type: "line",
        smooth: true,
        showSymbol: false,
        data: ys,
        lineStyle: { width: 2.5, color: "#10b981" },
        itemStyle: { color: "#10b981" },
        areaStyle: {
          color: {
            type: "linear",
            x: 0,
            y: 0,
            x2: 0,
            y2: 1,
            colorStops: [
              { offset: 0, color: "rgba(16,185,129,0.25)" },
              { offset: 1, color: "rgba(16,185,129,0.02)" },
            ],
          },
        },
        markLine: {
          silent: true,
          symbol: "none",
          lineStyle: { type: "dashed", color: "#f59e0b", width: 1 },
          data: [
            {
              yAxis: m.warn,
              label: { formatter: "预警线", color: "#f59e0b", fontSize: 10 },
            },
          ],
        },
      },
    ],
  };
}

function renderChart() {
  if (!chartEl.value) return;
  if (!chart) {
    chart = echarts.init(chartEl.value);
  }
  chart.setOption(buildOption(), true);
}

function resize() {
  chart && chart.resize();
}

watch(
  () => [selectedKey.value, history.value],
  () => nextTick(renderChart),
  { deep: true },
);

onMounted(() => {
  nextTick(renderChart);
  window.addEventListener("resize", resize);
});

onUnmounted(() => {
  window.removeEventListener("resize", resize);
  chart && chart.dispose();
  chart = null;
});
</script>

<template>
  <div class="dashboard">
    <!-- 顶部状态条 -->
    <div class="status-bar">
      <div class="status-left">
        <!-- 实时监测开关（mock 模式） -->
        <div v-show="dataSource === 'mock'" class="rt-switch">
          <span class="rt-label">实时监测</span>
          <span
            class="rt-track"
            :class="{ on: envData.active.value }"
            @click="envData.toggle()"
          >
            <span class="rt-thumb"></span>
          </span>
        </div>
        <!-- OneNET 模式用自动刷新状态 -->
        <span v-show="dataSource === 'onenet'" class="live-tag">
          <span
            class="live-dot"
            :class="{ offline: onenet.status.value === 'error' }"
          ></span>
          {{
            onenet.status.value === "ok"
              ? "实时"
              : onenet.status.value === "error"
                ? "离线"
                : "连接中"
          }}
        </span>
        <span class="update-text">
          最后更新：{{ lastUpdate ? formatFullTime(lastUpdate) : "-" }}
        </span>
      </div>

      <div class="status-center">
        <div class="ds-switch">
          <button
            class="ds-btn"
            :class="{ active: dataSource === 'mock' }"
            @click="dataSource = 'mock'"
          >
            模拟数据
          </button>
          <button
            class="ds-btn"
            :class="{ active: dataSource === 'onenet' }"
            @click="switchOnenet"
          >
            OneNET
          </button>
        </div>
        <input
          v-show="dataSource === 'onenet'"
          v-model="onenetDevice"
          class="device-input"
          type="text"
          placeholder="设备名 DeviceName"
          @keyup.enter="onenet.refresh()"
        />
        <button
          v-show="dataSource === 'onenet'"
          class="refresh-btn"
          :disabled="onenet.status.value === 'loading'"
          @click="onenet.refresh()"
        >
          刷新
        </button>
      </div>

      <div class="status-right">
        <span
          v-if="dataSource === 'onenet'"
          class="ds-status"
          :class="onenet.status.value"
        >
          {{ onenetStatusText }}
        </span>
        <!-- 设备在线/离线指示（仅 OneNET 模式） -->
        <span
          v-if="dataSource === 'onenet' && onenet.status.value === 'ok'"
          class="device-status"
          :class="{
            online: onenet.deviceOnline.value,
            offline: !onenet.deviceOnline.value,
          }"
          :title="
            onenet.deviceLastSeen
              ? '最后上报: ' + formatFullTime(onenet.deviceLastSeen)
              : ''
          "
        >
          <span
            class="dev-dot"
            :class="{ blink: onenet.deviceOnline.value }"
          ></span>
          {{ deviceOnlineText }}
        </span>
        <span v-else>共 {{ metrics.length }} 项指标 · 数据每 5 秒刷新</span>
      </div>
    </div>

    <!-- 指标卡片 -->
    <div class="cards">
      <div
        v-for="card in cards"
        :key="card.key"
        class="card"
        :class="{ active: selectedKey === card.key }"
        @click="selectedKey = card.key"
      >
        <div class="card-head">
          <span class="card-icon">{{ card.icon }}</span>
          <span class="card-name">{{ card.name }}</span>
        </div>
        <div class="card-value" :style="{ color: card.level?.color }">
          {{ card.value }}
          <span class="card-unit">{{ card.unit }}</span>
        </div>
        <div class="card-level" :style="{ color: card.level?.color }">
          <span
            class="level-dot"
            :style="{ background: card.level?.color }"
          ></span>
          {{ card.level?.label }}
        </div>
      </div>
    </div>

    <!-- 设备控制面板 -->
    <div
      v-if="
        (dataSource === 'onenet' && onenet.controllableMetrics.value.length) ||
        dataSource === 'mock'
      "
      class="control-panel"
    >
      <div class="control-head">
        <span class="control-title">🎛 设备控制</span>
        <span v-if="controlLoading" class="control-loading">命令发送中...</span>
        <span
          v-if="controlMsg"
          class="control-msg"
          :class="{ error: controlMsg.startsWith('失败') }"
        >
          {{ controlMsg }}
        </span>
      </div>

      <!-- OneNET 模式：显示物模型里的读写属性 -->
      <div v-if="dataSource === 'onenet'" class="control-items">
        <div
          v-for="m in onenet.controllableMetrics.value"
          :key="m.key"
          class="control-item"
        >
          <div class="control-meta">
            <span class="control-icon">{{ m.icon }}</span>
            <span class="control-name">{{ m.name }}</span>
            <span class="control-current">
              当前: <b>{{ current[m.key] ?? "-" }}</b>
            </span>
          </div>
          <div class="control-btns">
            <button
              class="ctrl-btn off"
              :disabled="controlLoading || current[m.key] === 0"
              @click="sendControl(m, 0)"
            >
              关 (0)
            </button>
            <button
              class="ctrl-btn on"
              :disabled="controlLoading || current[m.key] === 1"
              @click="sendControl(m, 1)"
            >
              开 (1)
            </button>
          </div>
        </div>
      </div>

      <!-- Mock 模式：显示虚拟开关，开了会影响模拟数据 -->
      <div v-else class="control-items">
        <div v-for="c in MOCK_CONTROLS" :key="c.key" class="control-item">
          <div class="control-meta">
            <span class="control-icon">{{ c.icon }}</span>
            <span class="control-name">{{ c.name }}</span>
            <span v-if="Object.keys(c.effect).length" class="control-hint">
              {{ effectLabel(c.effect) }}
            </span>
          </div>
          <div class="control-btns">
            <button
              class="ctrl-btn off"
              :disabled="mockSwitchState[c.key] === 0"
              @click="toggleMock(c.key, 0)"
            >
              关
            </button>
            <button
              class="ctrl-btn on"
              :disabled="mockSwitchState[c.key] === 1"
              @click="toggleMock(c.key, 1)"
            >
              开
            </button>
          </div>
        </div>
      </div>
    </div>

    <!-- AI 对话面板 -->
    <div class="ai-panel">
      <div class="ai-head">
        <span class="ai-title">🤖 AI 环境助手</span>
        <div class="ai-actions">
          <button class="ai-quick" :disabled="aiLoading" @click="quickAnalyze">
            一键环境分析
          </button>
          <button
            class="ai-clear"
            v-if="aiMessages.length"
            :disabled="aiLoading"
            @click="clearChat"
          >
            清空
          </button>
        </div>
      </div>
      <div class="ai-chat-body" ref="chatBodyRef">
        <div v-if="!aiMessages.length" class="ai-empty">
          💬 可以直接提问，例如「现在温度合适吗？」「需要开风扇吗？」<br />
          或点击「一键环境分析」让 AI 自动评估当前环境。
        </div>
        <div
          v-for="(msg, i) in aiMessages"
          :key="i"
          class="ai-msg"
          :class="msg.role"
        >
          <div class="ai-msg-avatar">
            {{ msg.role === "user" ? "🧑" : "🤖" }}
          </div>
          <div class="ai-msg-bubble">
            <div v-if="msg.error" class="ai-msg-error">❌ {{ msg.error }}</div>
            <div v-else class="ai-msg-content">{{ msg.content }}</div>
            <div
              v-if="msg.deviceChanges && msg.deviceChanges.length"
              class="ai-device-changes"
            >
              <div
                v-for="(ch, ci) in msg.deviceChanges"
                :key="ci"
                class="ai-device-tag"
                :class="ch.value ? 'on' : 'off'"
              >
                {{ ch.name }} → {{ ch.value ? "开" : "关" }}
              </div>
            </div>
          </div>
        </div>
        <div v-if="aiLoading" class="ai-msg assistant">
          <div class="ai-msg-avatar">🤖</div>
          <div class="ai-msg-bubble">
            <span class="ai-typing"> <i></i><i></i><i></i> </span>
          </div>
        </div>
      </div>
      <div class="ai-input-row">
        <textarea
          v-model="aiInput"
          class="ai-input"
          placeholder="输入消息，Enter 发送 / Shift+Enter 换行"
          rows="2"
          @keydown="onAiKeydown"
          :disabled="aiLoading"
        ></textarea>
        <button
          class="ai-send"
          :disabled="aiLoading || !aiInput.trim()"
          @click="sendChat()"
        >
          {{ aiLoading ? "..." : "发送" }}
        </button>
      </div>
    </div>

    <!-- 趋势图 -->
    <div class="chart-section">
      <div class="chart-head">
        <div class="chart-title">
          {{ selectedMetric.name }} 24 小时趋势
          <span class="chart-unit">（{{ selectedMetric.unit }}）</span>
        </div>
        <div class="chart-legend">
          <span class="legend-item"><i class="legend-line"></i>实测值</span>
          <span class="legend-item warn"
            ><i class="legend-line dashed"></i>预警阈值</span
          >
        </div>
      </div>
      <div ref="chartEl" class="chart"></div>
    </div>
  </div>
</template>

<style scoped>
.dashboard {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.status-bar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 16px;
  background: var(--bg-card);
  border-radius: var(--radius);
  padding: 12px 16px;
  box-shadow: var(--shadow-card);
  flex-wrap: wrap;
}

.status-left {
  display: flex;
  align-items: center;
  gap: 12px;
}

.status-center {
  display: flex;
  align-items: center;
  gap: 8px;
  flex: 1;
  justify-content: center;
  min-width: 0;
}

.ds-switch {
  display: inline-flex;
  background: var(--bg-page);
  border-radius: 6px;
  padding: 2px;
}

.ds-btn {
  padding: 5px 12px;
  font-size: 12px;
  color: var(--color-text-secondary);
  border-radius: 4px;
  transition: all 0.15s;
}

.ds-btn:hover {
  color: var(--color-text);
}

.ds-btn.active {
  background: var(--bg-card);
  color: var(--color-primary-dark);
  font-weight: 500;
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.08);
}

.device-input {
  padding: 6px 10px;
  font-size: 12px;
  border: 1px solid var(--color-border);
  border-radius: 6px;
  width: 180px;
  outline: none;
  transition: border-color 0.2s;
}

.device-input:focus {
  border-color: var(--color-primary);
}

.refresh-btn {
  padding: 6px 12px;
  font-size: 12px;
  background: var(--color-primary);
  color: #fff;
  border-radius: 6px;
  transition: opacity 0.15s;
}

.refresh-btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.ds-status {
  font-size: 12px;
  padding: 2px 8px;
  border-radius: 4px;
}

.ds-status.ok {
  color: var(--color-primary);
  background: rgba(16, 185, 129, 0.1);
}

.ds-status.loading {
  color: var(--color-info);
  background: rgba(59, 130, 246, 0.1);
}

.ds-status.error {
  color: var(--color-danger);
  background: rgba(239, 68, 68, 0.1);
}

/* 设备在线/离线指示器 */
.device-status {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  padding: 2px 10px;
  border-radius: 4px;
  margin-left: 8px;
  white-space: nowrap;
  transition: all 0.2s;
}

.device-status.online {
  color: #10b981;
  background: rgba(16, 185, 129, 0.1);
}

.device-status.offline {
  color: #ef4444;
  background: rgba(239, 68, 68, 0.1);
}

.dev-dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  flex-shrink: 0;
}

.device-status.online .dev-dot {
  background: #10b981;
}

.device-status.offline .dev-dot {
  background: #ef4444;
}

.dev-dot.blink {
  animation: blink 1.4s infinite;
}

.live-tag {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  background: rgba(16, 185, 129, 0.1);
  color: var(--color-primary-dark);
  padding: 3px 10px;
  border-radius: 12px;
  font-size: 12px;
  font-weight: 500;
}

.live-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: var(--color-primary);
  animation: blink 1.4s infinite;
}
.live-dot.offline {
  background: #9ca3af;
  animation: none;
}

/* 实时监测开关 */
.rt-switch {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  cursor: pointer;
  user-select: none;
}
.rt-label {
  font-size: 13px;
  color: var(--text-primary);
  font-weight: 500;
}
.rt-track {
  width: 40px;
  height: 20px;
  border-radius: 10px;
  background: #d1d5db;
  position: relative;
  transition: background 0.25s;
}
.rt-track.on {
  background: var(--color-primary);
}
.rt-thumb {
  position: absolute;
  top: 2px;
  left: 2px;
  width: 16px;
  height: 16px;
  border-radius: 50%;
  background: #fff;
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.2);
  transition: transform 0.25s;
}
.rt-track.on .rt-thumb {
  transform: translateX(20px);
}

@keyframes blink {
  0%,
  100% {
    opacity: 1;
  }
  50% {
    opacity: 0.3;
  }
}

.update-text {
  font-size: 13px;
  color: var(--color-text-secondary);
}

.status-right {
  font-size: 12px;
  color: var(--color-text-secondary);
}

.cards {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
  gap: 12px;
}

.card {
  background: var(--bg-card);
  border: 1px solid var(--color-border);
  border-radius: var(--radius);
  padding: 16px;
  cursor: pointer;
  transition: all 0.2s;
  box-shadow: var(--shadow-card);
}

.card:hover {
  transform: translateY(-2px);
  box-shadow: var(--shadow-card-hover);
}

.card.active {
  border-color: var(--color-primary);
  box-shadow: 0 0 0 3px rgba(16, 185, 129, 0.12);
}

.card-head {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 12px;
}

.card-icon {
  font-size: 18px;
}

.card-name {
  font-size: 13px;
  color: var(--color-text-secondary);
}

.card-value {
  font-size: 28px;
  font-weight: 600;
  line-height: 1.1;
  margin-bottom: 8px;
}

.card-unit {
  font-size: 13px;
  font-weight: 400;
  margin-left: 2px;
}

.card-level {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
}

.level-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
}

.chart-section {
  background: var(--bg-card);
  border-radius: var(--radius);
  padding: 16px;
  box-shadow: var(--shadow-card);
}

.chart-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 8px;
}

.chart-title {
  font-size: 15px;
  font-weight: 500;
}

.chart-unit {
  font-size: 12px;
  color: var(--color-text-secondary);
  font-weight: 400;
}

.chart-legend {
  display: flex;
  gap: 16px;
  font-size: 12px;
  color: var(--color-text-secondary);
}

.legend-item {
  display: inline-flex;
  align-items: center;
  gap: 6px;
}

.legend-line {
  display: inline-block;
  width: 16px;
  height: 2px;
  background: var(--color-primary);
}

.legend-line.dashed {
  background: transparent;
  border-top: 2px dashed var(--color-warning);
}

.chart {
  width: 100%;
  height: 320px;
}

/* ===== 设备控制面板 ===== */
.control-panel {
  background: var(--bg-card);
  border-radius: var(--radius);
  padding: 16px 20px;
  box-shadow: var(--shadow-card);
}

.control-head {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-bottom: 14px;
  padding-bottom: 10px;
  border-bottom: 1px solid var(--color-border);
}

.control-title {
  font-weight: 600;
  font-size: 15px;
  color: var(--text-primary);
}

.control-loading {
  font-size: 12px;
  color: var(--color-primary);
  animation: blink 1.2s infinite;
}

.control-msg {
  font-size: 12px;
  color: #10b981;
  padding: 2px 8px;
  border-radius: 4px;
  background: rgba(16, 185, 129, 0.1);
}
.control-msg.error {
  color: #ef4444;
  background: rgba(239, 68, 68, 0.1);
}

@keyframes blink {
  0%,
  100% {
    opacity: 1;
  }
  50% {
    opacity: 0.4;
  }
}

.control-items {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(260px, 1fr));
  gap: 12px;
}

.control-item {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 14px;
  background: var(--bg-page);
  border: 1px solid var(--color-border);
  border-radius: 8px;
  transition: border-color 0.2s;
}
.control-item:hover {
  border-color: var(--color-primary);
}

.control-meta {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 14px;
}

.control-icon {
  font-size: 18px;
}
.control-name {
  color: var(--text-primary);
  font-weight: 500;
}
.control-current {
  color: var(--text-muted);
  font-size: 12px;
}
.control-current b {
  color: var(--color-primary);
}
.control-hint {
  font-size: 11px;
  color: var(--text-muted);
  padding: 1px 6px;
  background: rgba(99, 102, 241, 0.1);
  border-radius: 4px;
}

.control-btns {
  display: flex;
  gap: 6px;
}

.ctrl-btn {
  padding: 5px 14px;
  border: 1px solid var(--color-border);
  border-radius: 6px;
  font-size: 12px;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s;
}
.ctrl-btn.off {
  background: var(--bg-card);
  color: var(--text-muted);
}
.ctrl-btn.on {
  background: var(--color-primary);
  color: #fff;
  border-color: var(--color-primary);
}
.ctrl-btn:hover:not(:disabled) {
  transform: translateY(-1px);
  box-shadow: 0 2px 6px rgba(0, 0, 0, 0.1);
}
.ctrl-btn:disabled {
  opacity: 0.4;
  cursor: not-allowed;
}

/* ===== AI 对话面板 ===== */
.ai-panel {
  background: linear-gradient(
    135deg,
    rgba(99, 102, 241, 0.08) 0%,
    rgba(139, 92, 246, 0.08) 100%
  );
  border: 1px solid rgba(99, 102, 241, 0.2);
  border-radius: var(--radius);
  padding: 16px 20px;
  display: flex;
  flex-direction: column;
  min-height: 320px;
}

.ai-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 12px;
}

.ai-title {
  font-weight: 600;
  font-size: 15px;
  color: var(--text-primary);
}

.ai-actions {
  display: flex;
  gap: 8px;
}

.ai-quick,
.ai-clear,
.ai-send {
  padding: 6px 14px;
  border: none;
  border-radius: 8px;
  font-size: 13px;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s;
}

.ai-quick {
  background: linear-gradient(135deg, #6366f1, #8b5cf6);
  color: #fff;
}
.ai-quick:hover:not(:disabled) {
  transform: translateY(-1px);
  box-shadow: 0 4px 12px rgba(99, 102, 241, 0.3);
}

.ai-clear {
  background: rgba(107, 114, 128, 0.15);
  color: var(--text-secondary);
}
.ai-clear:hover:not(:disabled) {
  background: rgba(107, 114, 128, 0.25);
}

.ai-quick:disabled,
.ai-clear:disabled,
.ai-send:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.ai-chat-body {
  flex: 1;
  min-height: 180px;
  max-height: 360px;
  overflow-y: auto;
  padding: 8px 4px;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.ai-empty {
  color: var(--text-muted);
  font-size: 13px;
  font-style: italic;
  text-align: center;
  padding: 30px 12px;
  line-height: 1.8;
}

.ai-msg {
  display: flex;
  gap: 8px;
  align-items: flex-start;
}

.ai-msg.user {
  flex-direction: row-reverse;
}

.ai-msg-avatar {
  width: 28px;
  height: 28px;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 18px;
  flex-shrink: 0;
}

.ai-msg-bubble {
  max-width: 80%;
  padding: 10px 14px;
  border-radius: 12px;
  font-size: 14px;
  line-height: 1.6;
  word-break: break-word;
}

.ai-msg.user .ai-msg-bubble {
  background: linear-gradient(135deg, #6366f1, #8b5cf6);
  color: #fff;
  border-top-right-radius: 4px;
}

.ai-msg.assistant .ai-msg-bubble {
  background: rgba(255, 255, 255, 0.08);
  color: var(--text-primary);
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-top-left-radius: 4px;
}

.ai-msg-content {
  white-space: pre-wrap;
}

.ai-msg-error {
  color: #ef4444;
}

.ai-device-changes {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  margin-top: 8px;
  padding-top: 8px;
  border-top: 1px dashed rgba(255, 255, 255, 0.15);
}

.ai-device-tag {
  padding: 3px 10px;
  border-radius: 12px;
  font-size: 12px;
  font-weight: 600;
}

.ai-device-tag.on {
  background: rgba(34, 197, 94, 0.2);
  color: #22c55e;
  border: 1px solid rgba(34, 197, 94, 0.4);
}

.ai-device-tag.off {
  background: rgba(107, 114, 128, 0.2);
  color: #9ca3af;
  border: 1px solid rgba(107, 114, 128, 0.4);
}

.ai-typing {
  display: inline-flex;
  gap: 4px;
  align-items: center;
  padding: 4px 0;
}

.ai-typing i {
  width: 6px;
  height: 6px;
  background: #6366f1;
  border-radius: 50%;
  display: inline-block;
  animation: typing 1.2s infinite ease-in-out;
}

.ai-typing i:nth-child(2) {
  animation-delay: 0.2s;
}
.ai-typing i:nth-child(3) {
  animation-delay: 0.4s;
}

@keyframes typing {
  0%,
  60%,
  100% {
    transform: translateY(0);
    opacity: 0.4;
  }
  30% {
    transform: translateY(-4px);
    opacity: 1;
  }
}

.ai-input-row {
  display: flex;
  gap: 8px;
  margin-top: 12px;
  align-items: flex-end;
}

.ai-input {
  flex: 1;
  resize: none;
  padding: 8px 12px;
  background: rgba(255, 255, 255, 0.06);
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 8px;
  color: var(--text-primary);
  font-size: 14px;
  font-family: inherit;
  line-height: 1.5;
  outline: none;
  transition: border-color 0.2s;
}

.ai-input:focus {
  border-color: rgba(99, 102, 241, 0.5);
}

.ai-input:disabled {
  opacity: 0.6;
}

.ai-send {
  padding: 8px 18px;
  background: linear-gradient(135deg, #6366f1, #8b5cf6);
  color: #fff;
}
.ai-send:hover:not(:disabled) {
  transform: translateY(-1px);
  box-shadow: 0 4px 12px rgba(99, 102, 241, 0.3);
}

/* 滚动条 */
.ai-chat-body::-webkit-scrollbar {
  width: 6px;
}
.ai-chat-body::-webkit-scrollbar-thumb {
  background: rgba(99, 102, 241, 0.3);
  border-radius: 3px;
}
.ai-chat-body::-webkit-scrollbar-track {
  background: transparent;
}

/* 保留旧样式（兼容引用） */
.ai-body {
  min-height: 60px;
}
.ai-loading {
  display: flex;
  align-items: center;
  gap: 10px;
  color: var(--text-muted);
  font-size: 14px;
}
.ai-spinner {
  width: 16px;
  height: 16px;
  border: 2px solid rgba(99, 102, 241, 0.3);
  border-top-color: #6366f1;
  border-radius: 50%;
  animation: spin 0.8s linear infinite;
}
@keyframes spin {
  to {
    transform: rotate(360deg);
  }
}
.ai-content {
  color: var(--text-primary);
  font-size: 14px;
  line-height: 1.7;
  white-space: pre-wrap;
}
.ai-error {
  color: #ef4444;
  font-size: 13px;
  padding: 8px 12px;
  background: rgba(239, 68, 68, 0.1);
  border-radius: 6px;
}
</style>
