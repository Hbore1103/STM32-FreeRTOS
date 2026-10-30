<script setup>
import { ref, provide } from "vue";
import Dashboard from "./components/Dashboard.vue";
import AlarmList from "./components/AlarmList.vue";

const activeTab = ref("dashboard");
const dataSource = ref("mock"); // mock | onenet
const onenetDevice = ref(""); // OneNET 设备名，持久化供子组件复用
const tabs = [
  { key: "dashboard", label: "实时数据看板" },
  { key: "alarm", label: "告警信息列表" },
];

// 让子组件都能访问同一个数据源状态
provide("dataSource", dataSource);
provide("onenetDevice", onenetDevice);
</script>

<template>
  <div class="app">
    <header class="app-header">
      <div class="header-inner">
        <div class="brand">
          <span class="brand-logo">●</span>
          <span class="brand-name">环境监测系统</span>
        </div>
        <nav class="tabs">
          <button
            v-for="tab in tabs"
            :key="tab.key"
            class="tab"
            :class="{ active: activeTab === tab.key }"
            @click="activeTab = tab.key"
          >
            {{ tab.label }}
          </button>
        </nav>
        <div class="header-right">
          <span class="status-dot"></span>
          <span class="status-text">系统运行中</span>
        </div>
      </div>
    </header>

    <main class="app-main">
      <Dashboard v-show="activeTab === 'dashboard'" />
      <AlarmList v-show="activeTab === 'alarm'" />
    </main>
  </div>
</template>

<style scoped>
.app {
  display: flex;
  flex-direction: column;
  min-height: 100vh;
}

.app-header {
  background: linear-gradient(135deg, #059669 0%, #10b981 100%);
  color: #fff;
  box-shadow: 0 2px 8px rgba(16, 185, 129, 0.2);
  position: sticky;
  top: 0;
  z-index: 10;
}

.header-inner {
  max-width: 1400px;
  margin: 0 auto;
  padding: 0 24px;
  height: 64px;
  display: flex;
  align-items: center;
  gap: 32px;
}

.brand {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 18px;
  font-weight: 600;
}

.brand-logo {
  color: #fff;
  font-size: 20px;
  line-height: 1;
  text-shadow: 0 0 8px rgba(255, 255, 255, 0.6);
}

.tabs {
  display: flex;
  gap: 4px;
  flex: 1;
}

.tab {
  color: rgba(255, 255, 255, 0.8);
  font-size: 14px;
  padding: 8px 16px;
  border-radius: 6px;
  transition: all 0.2s;
}

.tab:hover {
  background: rgba(255, 255, 255, 0.1);
  color: #fff;
}

.tab.active {
  background: rgba(255, 255, 255, 0.2);
  color: #fff;
  font-weight: 500;
}

.header-right {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 13px;
  color: rgba(255, 255, 255, 0.9);
}

.status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: #fff;
  box-shadow: 0 0 0 0 rgba(255, 255, 255, 0.7);
  animation: pulse 2s infinite;
}

@keyframes pulse {
  0% {
    box-shadow: 0 0 0 0 rgba(255, 255, 255, 0.7);
  }
  70% {
    box-shadow: 0 0 0 8px rgba(255, 255, 255, 0);
  }
  100% {
    box-shadow: 0 0 0 0 rgba(255, 255, 255, 0);
  }
}

.app-main {
  flex: 1;
  max-width: 1400px;
  width: 100%;
  margin: 0 auto;
  padding: 24px;
}
</style>
