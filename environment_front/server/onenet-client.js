/**
 * OneNET Studio HTTP 客户端
 * 文档: https://open.iot.10086.cn/doc/aiot/develop/detail/1421
 *
 * 端点:
 *   - GET https://iot-api.heclouds.com/thingmodel/query-device-property
 *   - GET https://iot-api.heclouds.com/thingmodel/query-device-property-history
 */
import { generateToken } from "./onenet-auth.js";

const API_BASE = "https://iot-api.heclouds.com";

/**
 * 调用 OneNET API
 * @param {string} path 接口路径
 * @param {object} query GET 查询参数 / POST 请求体
 * @param {object} envConfig 环境配置 { userId, accessKey, productId }
 * @param {object} options { method: 'GET'|'POST', body: object }
 */
async function callApi(path, query, envConfig, options = {}) {
  const { method = "GET", body } = options;
  const url = new URL(API_BASE + path);

  if (method === "GET") {
    for (const [k, v] of Object.entries(query)) {
      if (v !== undefined && v !== null) url.searchParams.set(k, String(v));
    }
  }

  const resInfo = envConfig.resInfo || {
    type: "product",
    id: envConfig.productId,
  };
  const token = generateToken(resInfo, envConfig.accessKey);

  const fetchOpts = {
    method,
    headers: { Authorization: token, "Content-Type": "application/json" },
  };
  if (method === "POST" && body !== undefined) {
    fetchOpts.body = JSON.stringify(body);
  }

  const res = await fetch(url.toString(), fetchOpts);

  if (!res.ok) {
    const errBody = await res.text().catch(() => "");
    throw new Error(`OneNET API ${res.status}: ${errBody || res.statusText}`);
  }

  const data = await res.json();
  if (data.code !== 0 && data.success !== true) {
    throw new Error(`OneNET 业务错误: code=${data.code} msg=${data.msg}`);
  }
  return data.data ?? data;
}

/**
 * 查询产品物模型定义（属性、服务、事件）
 * @param {object} envConfig
 */
export function queryThingModel(envConfig) {
  return callApi(
    "/thingmodel/query-thing-model",
    { product_id: envConfig.productId },
    envConfig,
  );
}

/**
 * 查询设备最新属性数据
 * @param {object} envConfig
 * @param {string} deviceName 设备名称
 */
export function queryDeviceProperty(envConfig, deviceName) {
  return callApi(
    "/thingmodel/query-device-property",
    {
      product_id: envConfig.productId,
      device_name: deviceName,
    },
    envConfig,
  );
}

/**
 * 查询设备属性历史数据
 * @param {object} envConfig
 * @param {string} deviceName
 * @param {string} identifier 属性标识符
 * @param {number} startTimeMs 起始时间毫秒戳
 * @param {number} endTimeMs 结束时间毫秒戳
 * @param {number} limit 最多返回条数 1-100
 */
export function queryDevicePropertyHistory(
  envConfig,
  deviceName,
  identifier,
  startTimeMs,
  endTimeMs,
  limit = 100,
) {
  return callApi(
    "/thingmodel/query-device-property-history",
    {
      product_id: envConfig.productId,
      device_name: deviceName,
      identifier,
      start_time: String(startTimeMs),
      end_time: String(endTimeMs),
      limit: String(limit),
      sort: "2",
    },
    envConfig,
  );
}

/**
 * 设置设备属性（远程控制）
 * POST /thingmodel/set-device-desired-property
 * 备注：新版 Studio 用 desired-property，设备在线后自动下发
 *
 * @param {object} envConfig
 * @param {string} deviceName
 * @param {object} params  { Fan: 1, Beep: 0, ... }
 */
export function setDeviceProperty(envConfig, deviceName, params) {
  return callApi("/thingmodel/set-device-desired-property", {}, envConfig, {
    method: "POST",
    body: {
      product_id: envConfig.productId,
      device_name: deviceName,
      params,
    },
  });
}
