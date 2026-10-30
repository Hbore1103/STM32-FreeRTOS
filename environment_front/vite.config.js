import { defineConfig } from "vite";
import vue from "@vitejs/plugin-vue";

export default defineConfig({
  plugins: [vue()],
  server: {
    host: "0.0.0.0",
    port: 5173,
    open: true,
    proxy: {
      // 把前端的 /api 请求转发到后端 Koa 服务
      "/api": {
        target: "http://localhost:3001",
        changeOrigin: true,
      },
    },
  },
});
