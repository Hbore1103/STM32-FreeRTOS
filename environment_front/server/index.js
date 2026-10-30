/**
 * Koa 后端代理服务
 * 路由:
 *   GET  /api/health                                 健康检查
 *   GET  /api/onenet/thingmodel                      产品物模型定义
 *   GET  /api/onenet/property/:deviceName             设备最新属性
 *   GET  /api/onenet/history/:deviceName/:identifier  设备属性历史
 */
import Koa from "koa";
import Router from "@koa/router";
import cors from "@koa/cors";
import bodyParser from "@koa/bodyparser";
import "dotenv/config";
import {
  queryThingModel,
  queryDeviceProperty,
  queryDevicePropertyHistory,
  setDeviceProperty,
} from "./onenet-client.js";

const app = new Koa();
const router = new Router();

// 从环境变量读取配置
const envConfig = {
  userId: process.env.ONENET_USER_ID,
  accessKey: process.env.ONENET_ACCESS_KEY,
  productId: process.env.ONENET_PRODUCT_ID,
  defaultDevice: process.env.ONENET_DEVICE_NAME || "",
};

// 错误处理中间件
app.use(async (ctx, next) => {
  try {
    await next();
  } catch (err) {
    ctx.status = err.status || 500;
    ctx.body = { code: 1, msg: err.message };
    console.error("[server error]", err.message);
  }
});

app.use(cors());
app.use(bodyParser());
app.use(router.routes());
app.use(router.allowedMethods());

// 健康检查 + 配置自检
router.get("/api/health", (ctx) => {
  const ok = envConfig.userId && envConfig.accessKey && envConfig.productId;
  ctx.body = {
    code: 0,
    msg: "ok",
    data: {
      configured: Boolean(ok),
      productId: envConfig.productId || null,
      defaultDevice: envConfig.defaultDevice || null,
      hint: ok
        ? "配置完整"
        : "请在 .env 中填写 ONENET_USER_ID / ONENET_ACCESS_KEY / ONENET_PRODUCT_ID",
    },
  };
});

// 产品物模型定义
router.get("/api/onenet/thingmodel", async (ctx) => {
  const data = await queryThingModel(envConfig);
  ctx.body = { code: 0, msg: "ok", data };
});

// 设备最新属性
router.get("/api/onenet/property/:deviceName", async (ctx) => {
  const deviceName = ctx.params.deviceName || envConfig.defaultDevice;
  if (!deviceName) ctx.throw(400, "缺少 deviceName");
  const data = await queryDeviceProperty(envConfig, deviceName);
  ctx.body = { code: 0, msg: "ok", data };
});

// 设备属性历史
router.get("/api/onenet/history/:deviceName/:identifier", async (ctx) => {
  const { deviceName, identifier } = ctx.params;
  const end = Date.now();
  const start = end - (Number(ctx.query.hours) || 2) * 3600 * 1000;
  const limit = Number(ctx.query.limit) || 100;

  const data = await queryDevicePropertyHistory(
    envConfig,
    deviceName || envConfig.defaultDevice,
    identifier,
    start,
    end,
    limit,
  );
  ctx.body = { code: 0, msg: "ok", data };
});

// 设置设备属性（远程控制）
// POST /api/onenet/set/:deviceName   body: { "Fan": 1, "Beep": 0 }
router.post("/api/onenet/set/:deviceName", async (ctx) => {
  const deviceName = ctx.params.deviceName || envConfig.defaultDevice;
  if (!deviceName) ctx.throw(400, "缺少 deviceName");
  const params = ctx.request.body;
  if (!params || typeof params !== "object") ctx.throw(400, "缺少 params");

  try {
    const data = await setDeviceProperty(envConfig, deviceName, params);
    ctx.body = { code: 0, msg: "ok", data };
  } catch (e) {
    // OneNET 可能返回 HTTP 200 但 code != 0，也可能网络错误
    ctx.body = { code: 1, msg: e.message };
  }
});

// 简单意图识别：从用户消息中识别设备控制指令
// 返回 [{ identifier, name, value }] 数组；匹配不到返回 []
function parseDeviceIntent(userText, controllable) {
  if (!userText || !Array.isArray(controllable)) return [];
  const results = [];
  const text = userText.toLowerCase();

  // 关闭/开启 关键词
  const turnOffWords = [
    "关",
    "关闭",
    "停止",
    "停下",
    "断开",
    "off",
    "stop",
    "关掉",
  ];
  const turnOnWords = [
    "开",
    "开启",
    "打开",
    "启动",
    "启动",
    "on",
    "start",
    "开起",
  ];

  for (const c of controllable) {
    const name = c.name.toLowerCase();
    const id = c.identifier.toLowerCase();
    // 设备名匹配（中文/英文）
    const hasDevice = text.includes(name) || text.includes(id);

    if (!hasDevice) {
      // 模糊匹配：通过同义词
      const aliases = {
        风扇: ["风扇", "电扇", "fan"],
        舵机: ["舵机", "servo", "门", "门锁"],
        蜂鸣器: ["蜂鸣器", "报警", "警铃", "beep", "alarm"],
        加湿器: ["加湿器", "加湿", "humidifier"],
        空调: ["空调", "制冷", "ac "],
      };
      const aliasList = aliases[name] || aliases[id] || [];
      if (!aliasList.some((a) => text.includes(a))) continue;
    }

    // 判断开还是关
    let value = null;
    // 优先判断关（"关"和"开"同时出现时，"关"更可能指关闭）
    for (const w of turnOffWords) {
      if (text.includes(w)) {
        value = 0;
        break;
      }
    }
    if (value === null) {
      for (const w of turnOnWords) {
        if (text.includes(w)) {
          value = 1;
          break;
        }
      }
    }

    if (value !== null) {
      results.push({ identifier: c.identifier, name: c.name, value });
    }
  }
  return results;
}

// 构造环境数据 system 消息
function buildEnvSystem(env) {
  const lines = [
    "你是环境监测系统的 AI 助手，可以基于实时传感器数据回答用户问题、给出建议。回答简洁、专业、用中文。",
  ];
  if (env && typeof env === "object") {
    const parts = [];
    if (env.temperature != null) parts.push(`温度 ${env.temperature}°C`);
    if (env.humidity != null) parts.push(`湿度 ${env.humidity}%`);
    if (Array.isArray(env.metrics) && env.metrics.length) {
      parts.push(
        "其他指标: " +
          env.metrics
            .map((m) => `${m.name}=${m.value}${m.unit || ""}`)
            .join("、"),
      );
    }
    if (parts.length) lines.push("当前传感器读数: " + parts.join("，") + "。");
    // 告知 AI 有设备控制权限
    if (Array.isArray(env.controllable) && env.controllable.length) {
      const list = env.controllable
        .map(
          (c) => `${c.name}(${c.identifier})可选值${JSON.stringify(c.values)}`,
        )
        .join("、");
      lines.push(
        `你有权限直接控制以下设备：${list}。当用户的请求明确或隐含需要控制设备时（如"开风扇"、"太热了"、"把舵机关了"、"关闭蜂鸣器"、"不用了关掉吧"、"停止风扇"等），无论开启还是关闭，都请直接调用对应工具执行操作，传入 1 表示开、0 表示关，不要只回复建议或反问用户。操作完成后用一句话告知用户执行结果。`,
      );
      // few-shot 示例：帮助 AI 理解何时调工具
      lines.push("");
      lines.push("以下是一些应该调用工具的示例：");
      lines.push('- 用户说"开风扇" → 调用 set_Fan，value=1');
      lines.push(
        '- 用户说"关风扇"或"关闭风扇"或"停止风扇" → 调用 set_Fan，value=0',
      );
      lines.push('- 用户说"太热了" → 调用 set_Fan，value=1');
      lines.push(
        '- 用户说"不用了关掉吧"（前文讨论过风扇） → 调用 set_Fan，value=0',
      );
      lines.push('- 用户说"打开舵机" → 调用 set_Servo，value=1');
      lines.push('- 用户说"把舵机关了" → 调用 set_Servo，value=0');
      lines.push('- 用户说"开蜂鸣器"或"报警" → 调用 set_Beep，value=1');
      lines.push('- 用户说"关蜂鸣器"或"停止报警" → 调用 set_Beep，value=0');
      lines.push(
        '当用户只是询问环境状况（如"现在多少度"），不要调用工具，直接回答。',
      );
    }
  }
  return lines.join("\n");
}

// AI 分析：接收环境数据，调用 DeepSeek 生成建议（保留原接口，兼容旧前端）
// POST /api/ai/analyze   body: { temperature, humidity, history }
router.post("/api/ai/analyze", async (ctx) => {
  const apiKey = process.env.DEEPSEEK_API_KEY;
  const model = process.env.DEEPSEEK_MODEL || "deepseek-chat";

  if (!apiKey) {
    ctx.status = 503;
    ctx.body = { code: 1, msg: "AI 未配置：缺少 DEEPSEEK_API_KEY" };
    return;
  }

  const body = ctx.request.body || {};
  const { temperature, humidity, history } = body;

  const dataSummary = [
    `当前温度: ${temperature ?? "未知"}°C`,
    `当前湿度: ${humidity ?? "未知"}%`,
  ];
  if (history?.length) {
    const recent = history
      .slice(-6)
      .map((p) => `${p.time}时 ${p.value}°C`)
      .join(", ");
    dataSummary.push(`近几小时温度变化: ${recent}`);
  }

  const prompt = `你是一个环境监测 AI 助手。请根据以下传感器数据，给出：
1. 当前环境状态评估（一句话）
2. 健康影响提示（如有）
3. 建议措施（如开风扇、关窗、加湿等，分条列出）
4. 简洁的一句话总结

请用中文回答，控制在 200 字以内。

传感器数据：
${dataSummary.join("\n")}`;

  try {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 15000);

    const res = await fetch("https://api.deepseek.com/v1/chat/completions", {
      method: "POST",
      headers: {
        Authorization: `Bearer ${apiKey}`,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        model,
        messages: [{ role: "user", content: prompt }],
        max_tokens: 600,
        temperature: 0.4,
      }),
      signal: controller.signal,
    });
    clearTimeout(timer);

    if (!res.ok) {
      const txt = await res.text().catch(() => "");
      ctx.body = {
        code: 1,
        msg: `DeepSeek ${res.status}: ${txt || res.statusText}`,
      };
      return;
    }

    const data = await res.json();
    const raw = data?.choices?.[0]?.message?.content;
    const content = raw ? raw.trim() : "(AI 无响应)";
    ctx.body = { code: 0, msg: "ok", data: { content } };
  } catch (e) {
    if (e.name === "AbortError") {
      ctx.body = { code: 1, msg: "AI 响应超时，请重试" };
    } else {
      ctx.body = { code: 1, msg: `AI 调用失败: ${e.message}` };
    }
  }
});

// AI 对话：多轮对话，环境数据作为 system 注入；支持 function calling 控制设备
// POST /api/ai/chat   body: { messages: [{role,content}], env: {temperature,humidity,metrics,controllable} }
router.post("/api/ai/chat", async (ctx) => {
  const apiKey = process.env.DEEPSEEK_API_KEY;
  const model = process.env.DEEPSEEK_MODEL || "deepseek-chat";

  if (!apiKey) {
    ctx.status = 503;
    ctx.body = { code: 1, msg: "AI 未配置：缺少 DEEPSEEK_API_KEY" };
    return;
  }

  const body = ctx.request.body || {};
  const userMessages = Array.isArray(body.messages) ? body.messages : [];
  const env = body.env || {};

  if (!userMessages.length) {
    ctx.body = { code: 1, msg: "messages 不能为空" };
    return;
  }

  // 工具：让 AI 控制设备属性（如风扇、舵机、蜂鸣器）
  // 可控属性从 env.controllable 动态生成；若无则用默认列表
  const controllable =
    Array.isArray(env.controllable) && env.controllable.length
      ? env.controllable
      : [
          { identifier: "Fan", name: "风扇", values: [0, 1] },
          { identifier: "Servo", name: "舵机", values: [0, 1] },
          { identifier: "Beep", name: "蜂鸣器", values: [0, 1] },
        ];

  // === 第一步：规则意图识别（比 AI tool_call 更稳定） ===
  const latestUserMsg = userMessages[userMessages.length - 1];
  const userText =
    latestUserMsg && latestUserMsg.role === "user" ? latestUserMsg.content : "";

  const intents = parseDeviceIntent(userText, controllable);
  const deviceChanges = [];
  const executedActions = []; // 用于告知 AI 已执行的操作

  // mock 模式：deviceName 为 "mock-device" 时不实际下发到 OneNET，只返回意图让前端处理
  const isMockDevice = env.deviceName === "mock-device" || env.mock === true;

  if (intents.length) {
    for (const intent of intents) {
      if (isMockDevice) {
        // mock 模式：直接记为成功，让前端自己改本地状态
        deviceChanges.push({
          identifier: intent.identifier,
          name: intent.name,
          value: intent.value,
        });
        executedActions.push(
          `${intent.name}(${intent.identifier})已设置为${intent.value === 1 ? "开" : "关"}`,
        );
      } else {
        try {
          await setDeviceProperty(
            envConfig,
            env.deviceName || envConfig.defaultDevice,
            { [intent.identifier]: intent.value },
          );
          deviceChanges.push({
            identifier: intent.identifier,
            name: intent.name,
            value: intent.value,
          });
          executedActions.push(
            `${intent.name}(${intent.identifier})已设置为${intent.value === 1 ? "开" : "关"}`,
          );
        } catch (e) {
          executedActions.push(`${intent.name}控制失败: ${e.message}`);
        }
      }
    }
  }

  // 组装最终消息：system + 历史对话 + 已执行操作提示
  const sys = buildEnvSystem(env);
  const messages = [{ role: "system", content: sys }, ...userMessages];

  // 如果有已执行的操作，在最后一条 user 消息后追加事实提示
  if (executedActions.length) {
    messages.push({
      role: "system",
      content: `系统已根据用户上一条消息的意图，执行了以下设备控制操作：\n${executedActions.join("\n")}\n请基于此事实回复用户，简洁告知执行结果，可结合当前环境数据给出一句建议。`,
    });
  }

  const tools = controllable.map((c) => ({
    type: "function",
    function: {
      name: `set_${c.identifier}`,
      description: `控制${c.name}（${c.identifier}）。${
        c.values.includes(0) && c.values.includes(1)
          ? "传入 1 表示开，传入 0 表示关。"
          : `可选值: ${c.values.join(", ")}。`
      }`,
      parameters: {
        type: "object",
        properties: {
          value: {
            type: "number",
            enum: c.values,
            description: `${c.name}的目标状态`,
          },
        },
        required: ["value"],
      },
    },
  }));

  // 如果规则已执行操作，就不再让 AI 调用工具（避免重复执行）
  const toolsToUse = executedActions.length ? [] : tools;

  try {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 30000); // 30s（含工具调用）

    // 循环处理 tool_calls（最多 2 轮，防死循环）
    let round = 0;
    let lastData = null;
    while (round < 3) {
      const res = await fetch("https://api.deepseek.com/v1/chat/completions", {
        method: "POST",
        headers: {
          Authorization: `Bearer ${apiKey}`,
          "Content-Type": "application/json",
        },
        body: JSON.stringify({
          model,
          messages,
          tools: toolsToUse,
          tool_choice: toolsToUse.length ? "auto" : undefined,
          max_tokens: 800,
          temperature: 0.5,
        }),
        signal: controller.signal,
      });

      if (!res.ok) {
        const txt = await res.text().catch(() => "");
        ctx.body = {
          code: 1,
          msg: `DeepSeek ${res.status}: ${txt || res.statusText}`,
        };
        clearTimeout(timer);
        return;
      }

      const data = await res.json();
      lastData = data;
      const choice = data?.choices?.[0];
      const msg = choice?.message;

      // 没有 tool_calls，说明 AI 已经准备好最终回复
      if (!msg?.tool_calls?.length) break;

      // 把 AI 的 tool_call 消息加入历史
      messages.push(msg);

      // 执行每个工具调用
      for (const call of msg.tool_calls) {
        const fnName = call.function.name; // 如 "set_Fan"
        const target = controllable.find(
          (c) => `set_${c.identifier}` === fnName,
        );
        let resultText = "";
        if (!target) {
          resultText = `未知设备: ${fnName}`;
        } else {
          let parsed = {};
          try {
            parsed = JSON.parse(call.function.arguments || "{}");
          } catch (_) {
            resultText = "参数解析失败";
          }
          const value = parsed.value;
          if (value === undefined || value === null) {
            resultText = "缺少 value 参数";
          } else {
            try {
              const r = await setDeviceProperty(
                envConfig,
                env.deviceName || envConfig.defaultDevice,
                { [target.identifier]: value },
              );
              resultText = `已设置${target.name}(${target.identifier})=${value}。OneNET 返回: ${JSON.stringify(r).slice(0, 200)}`;
              deviceChanges.push({
                identifier: target.identifier,
                name: target.name,
                value,
              });
            } catch (e) {
              resultText = `设置${target.name}失败: ${e.message}`;
            }
          }
        }
        messages.push({
          role: "tool",
          tool_call_id: call.id,
          content: resultText,
        });
      }

      round++;
    }

    clearTimeout(timer);

    const raw = lastData?.choices?.[0]?.message?.content;
    const content = raw ? raw.trim() : "(AI 无响应)";
    ctx.body = {
      code: 0,
      msg: "ok",
      data: { content, deviceChanges },
    };
  } catch (e) {
    if (e.name === "AbortError") {
      ctx.body = { code: 1, msg: "AI 响应超时，请重试" };
    } else {
      ctx.body = { code: 1, msg: `AI 调用失败: ${e.message}` };
    }
  }
});

const PORT = process.env.SERVER_PORT || 3001;
app.listen(PORT, () => {
  console.log(`[server] running at http://localhost:${PORT}`);
  console.log(`[server] productId=${envConfig.productId || "(未配置)"}`);
});
