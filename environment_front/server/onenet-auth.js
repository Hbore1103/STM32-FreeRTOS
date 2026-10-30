/**
 * OneNET Studio 鉴权 Token 生成
 * 官方文档: https://open.iot.10086.cn/doc/v5/fuse/detail/1464
 *
 * Token 格式:
 *   version=2022-05-01&res={res}&et={ts}&method=sha1&sign={sign}
 *
 * sign 算法 (严格按官方):
 *   StringForSignature = et + "\n" + method + "\n" + res + "\n" + version
 *   sign = base64(hmac_sha1(base64decode(accessKey), utf8(StringForSignature)))
 *
 * res 取值（三选一，取决于你持有的 accessKey 类型）:
 *   用户级 accessKey -> res = userid/{userId}
 *   产品级 accessKey -> res = products/{productId}
 *   项目级 accessKey -> res = projects/{projectId}
 *
 * value 部分需 URL 编码: + -> %2B, / -> %2F, = -> %3D ...
 */
import crypto from "node:crypto";

const VERSION = "2022-05-01";
const METHOD = "sha1";

/**
 * 生成 OneNET API 鉴权 token
 * @param {object} resInfo 资源信息 { type: 'user'|'product'|'project', id: string }
 * @param {string} accessKey 对应的 access_key（需与 res 类型匹配）
 * @param {number} expireSeconds token 有效期（秒），默认 1 小时
 * @returns {string} 完整 token 字符串
 */
export function generateToken(resInfo, accessKey, expireSeconds = 3600) {
  const et = String(Math.floor(Date.now() / 1000) + expireSeconds);

  // 按 res 类型拼接资源路径
  const resTypeMap = {
    user: "userid",
    product: "products",
    project: "projects",
  };
  const res = `${resTypeMap[resInfo.type]}/${resInfo.id}`;

  // 签名串：et \n method \n res \n version（按字母序排列）
  const stringForSignature = `${et}\n${METHOD}\n${res}\n${VERSION}`;

  // accessKey 先 base64decode 再参与 HMAC
  const key = Buffer.from(accessKey, "base64");
  const signBuf = crypto
    .createHmac("sha1", key)
    .update(stringForSignature, "utf8")
    .digest();
  const sign = signBuf.toString("base64");

  // value URL 编码
  const enc = (v) => encodeURIComponent(v);

  return [
    `version=${VERSION}`,
    `res=${enc(res)}`,
    `et=${et}`,
    `method=${METHOD}`,
    `sign=${enc(sign)}`,
  ].join("&");
}
