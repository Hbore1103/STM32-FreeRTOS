<script setup>
import { ref, computed, inject } from 'vue'
import { useEnvData } from '../composables/useEnvData.js'
import { useOnenetData } from '../composables/useOnenetData.js'

const props = defineProps({ dataSource: { type: Object, required: true } })

const dataSource = inject('dataSource') || props.dataSource
const onenetDevice = ref('')
const envData = useEnvData()
const onenet = useOnenetData(onenetDevice)

// 按数据源取告警
const alarms = computed(() =>
  dataSource.value === 'onenet' ? onenet.alarms.value : envData.alarms.value
)

const filterStatus = ref('all')
const filterLevel = ref('all')
const search = ref('')

const statusOptions = [
  { key: 'all', label: '全部状态' },
  { key: 'pending', label: '待处理' },
  { key: 'processing', label: '处理中' },
  { key: 'resolved', label: '已解决' },
  { key: 'ignored', label: '已忽略' }
]

const levelOptions = [
  { key: 'all', label: '全部级别' },
  { key: 'warning', label: '预警' },
  { key: 'danger', label: '严重' }
]

const filtered = computed(() => {
  return alarms.value.filter((a) => {
    if (filterStatus.value !== 'all' && a.status !== filterStatus.value) return false
    if (filterLevel.value !== 'all' && a.level !== filterLevel.value) return false
    if (search.value) {
      const kw = search.value.toLowerCase()
      const hit =
        (a.stationName || '').toLowerCase().includes(kw) ||
        (a.alarmType || '').toLowerCase().includes(kw) ||
        (a.stationLocation || '').toLowerCase().includes(kw) ||
        (a.metricName || '').toLowerCase().includes(kw)
      if (!hit) return false
    }
    return true
  })
})

const stats = computed(() => {
  const list = alarms.value
  return {
    total: list.length,
    pending: list.filter((a) => a.status === 'pending').length,
    processing: list.filter((a) => a.status === 'processing').length,
    danger: list.filter((a) => a.level === 'danger').length
  }
})

const levelMeta = {
  warning: { label: '预警', color: '#f59e0b', bg: 'rgba(245, 158, 11, 0.1)' },
  danger: { label: '严重', color: '#ef4444', bg: 'rgba(239, 68, 68, 0.1)' }
}

function formatTime(ts) {
  if (!ts) return '-'
  const d = new Date(ts)
  const pad = (n) => String(n).padStart(2, '0')
  return `${d.getMonth() + 1}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}`
}
</script>

<template>
  <div class="alarm-page">
    <!-- 顶部数据源提示 -->
    <div class="ds-hint">
      当前数据源：
      <span class="ds-tag" :class="{ onenet: dataSource === 'onenet' }">
        {{ dataSource === 'onenet' ? 'OneNET 云平台' : '模拟数据' }}
      </span>
      <span class="ds-hint-text">
        {{ dataSource === 'onenet'
          ? '告警由设备上报值超出阈值自动触发'
          : '告警由模拟数据超过阈值自动生成（15s 待处理 → 处理中 → 已解决）' }}
      </span>
    </div>

    <!-- 统计卡片 -->
    <div class="stats">
      <div class="stat-card">
        <div class="stat-label">告警总数</div>
        <div class="stat-value">{{ stats.total }}</div>
      </div>
      <div class="stat-card warn">
        <div class="stat-label">待处理</div>
        <div class="stat-value">{{ stats.pending }}</div>
      </div>
      <div class="stat-card info">
        <div class="stat-label">处理中</div>
        <div class="stat-value">{{ stats.processing }}</div>
      </div>
      <div class="stat-card danger">
        <div class="stat-label">严重告警</div>
        <div class="stat-value">{{ stats.danger }}</div>
      </div>
    </div>

    <!-- 筛选区 -->
    <div class="toolbar">
      <div class="filter-group">
        <label>状态</label>
        <div class="btn-group">
          <button
            v-for="opt in statusOptions"
            :key="opt.key"
            class="filter-btn"
            :class="{ active: filterStatus === opt.key }"
            @click="filterStatus = opt.key"
          >{{ opt.label }}</button>
        </div>
      </div>
      <div class="filter-group">
        <label>级别</label>
        <div class="btn-group">
          <button
            v-for="opt in levelOptions"
            :key="opt.key"
            class="filter-btn"
            :class="{ active: filterLevel === opt.key }"
            @click="filterLevel = opt.key"
          >{{ opt.label }}</button>
        </div>
      </div>
      <div class="search-box">
        <input v-model="search" type="text" placeholder="搜索监测点 / 告警类型 / 指标" />
      </div>
    </div>

    <!-- 列表 -->
    <div class="table-wrap">
      <table class="table">
        <thead>
          <tr>
            <th>告警编号</th>
            <th>级别</th>
            <th>告警类型</th>
            <th>{{ dataSource === 'onenet' ? '设备名' : '监测点' }}</th>
            <th>指标</th>
            <th>实测值</th>
            <th>阈值</th>
            <th>状态</th>
            <th>发生时间</th>
            <th>解决时间</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="a in filtered" :key="a.id" :class="`row-${a.level}`">
            <td class="mono">{{ a.id }}</td>
            <td>
              <span
                class="level-tag"
                :style="{ color: levelMeta[a.level]?.color || '#6b7280', background: levelMeta[a.level]?.bg || 'rgba(107,114,128,0.1)' }"
              >
                {{ levelMeta[a.level]?.label || '未知' }}
              </span>
            </td>
            <td>{{ a.alarmType }}</td>
            <td>
              {{ a.stationName }}
              <span v-if="dataSource === 'mock'" class="muted-sm"> · {{ a.stationLocation }}</span>
            </td>
            <td>{{ a.metricName }}</td>
            <td class="mono">
              <b :style="{ color: levelMeta[a.level]?.color }">{{ a.value }}</b>
              <span class="muted-sm"> {{ a.unit }}</span>
            </td>
            <td class="mono muted-sm">{{ a.threshold }} {{ a.unit }}</td>
            <td>
              <span
                class="status-tag"
                :style="{ color: a.statusColor, background: (a.statusColor || '#6b7280') + '1a' }"
              >{{ a.statusLabel }}</span>
            </td>
            <td class="mono muted-sm">{{ formatTime(a.occurTime) }}</td>
            <td class="mono muted-sm">{{ formatTime(a.resolveTime) }}</td>
          </tr>
          <tr v-if="filtered.length === 0">
            <td colspan="10" class="empty">
              {{ alarms.length === 0 ? '暂无告警记录' : '没有匹配的告警记录' }}
            </td>
          </tr>
        </tbody>
      </table>
    </div>
  </div>
</template>

<style scoped>
.alarm-page { display: flex; flex-direction: column; gap: 16px; }

/* 数据源提示 */
.ds-hint {
  display: flex;
  align-items: center;
  gap: 10px;
  background: var(--bg-card);
  border-radius: var(--radius);
  padding: 10px 16px;
  box-shadow: var(--shadow-card);
  font-size: 13px;
}
.ds-tag {
  padding: 2px 10px;
  border-radius: 12px;
  font-weight: 500;
  background: rgba(16, 185, 129, 0.1);
  color: var(--color-primary-dark);
}
.ds-tag.onenet {
  background: rgba(99, 102, 241, 0.1);
  color: #6366f1;
}
.ds-hint-text {
  color: var(--color-text-secondary);
  font-size: 12px;
}

/* 统计卡片 */
.stats {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
  gap: 12px;
}
.stat-card {
  background: var(--bg-card);
  border-radius: var(--radius);
  padding: 16px 20px;
  box-shadow: var(--shadow-card);
  border-left: 4px solid var(--color-primary);
}
.stat-card.warn { border-left-color: var(--color-warning); }
.stat-card.info { border-left-color: var(--color-info); }
.stat-card.danger { border-left-color: var(--color-danger); }
.stat-label { font-size: 13px; color: var(--color-text-secondary); margin-bottom: 8px; }
.stat-value { font-size: 26px; font-weight: 600; color: var(--color-text); }

/* 工具栏 */
.toolbar {
  background: var(--bg-card);
  border-radius: var(--radius);
  padding: 12px 16px;
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 20px;
  box-shadow: var(--shadow-card);
}
.filter-group { display: flex; align-items: center; gap: 8px; }
.filter-group label { font-size: 13px; color: var(--color-text-secondary); }
.btn-group { display: flex; background: var(--bg-page); border-radius: 6px; padding: 2px; }
.filter-btn {
  padding: 5px 12px; font-size: 12px; color: var(--color-text-secondary);
  border-radius: 4px; transition: all 0.15s;
}
.filter-btn:hover { color: var(--color-text); }
.filter-btn.active {
  background: var(--bg-card); color: var(--color-primary-dark);
  font-weight: 500; box-shadow: 0 1px 2px rgba(0, 0, 0, 0.06);
}
.search-box { margin-left: auto; }
.search-box input {
  width: 260px; padding: 7px 12px; font-size: 13px;
  border: 1px solid var(--color-border); border-radius: 6px;
  outline: none; transition: border-color 0.2s;
}
.search-box input:focus { border-color: var(--color-primary); }

/* 表格 */
.table-wrap {
  background: var(--bg-card); border-radius: var(--radius);
  overflow: hidden; box-shadow: var(--shadow-card);
}
.table { width: 100%; border-collapse: collapse; font-size: 13px; }
.table thead th {
  background: #f9fafb; color: var(--color-text-secondary);
  font-weight: 500; text-align: left; padding: 10px 14px;
  border-bottom: 1px solid var(--color-border); white-space: nowrap;
}
.table tbody td {
  padding: 10px 14px; border-bottom: 1px solid #f3f4f6;
}
.table tbody tr:hover { background: #fafbfc; }

.mono { font-family: 'SF Mono', Consolas, Monaco, monospace; }
.muted-sm { color: var(--color-text-secondary); font-size: 12px; }

.level-tag, .status-tag {
  display: inline-block; padding: 2px 8px; border-radius: 4px;
  font-size: 12px; font-weight: 500;
}
.row-danger { background: rgba(239, 68, 68, 0.03); }
.empty { text-align: center; color: var(--color-text-secondary); padding: 40px 0; }
</style>
