#ifndef _onenet_token_H
#define _onenet_token_H

#include "system.h"

/*
 * OneNET新版物模型MQTT鉴权Token生成（无需官方token.exe，固件自动计算）
 *
 * 官方算法（https://open.iot.10086.cn/doc/mqtt/book/manual/auth/token.html）：
 *   StringForSignature = et + "\n" + method + "\n" + res + "\n" + version
 *   sign = base64( hmac_sha1( base64decode(设备密钥), StringForSignature ) )
 *   token = version=...&res=<URL编码>&et=...&method=sha1&sign=<URL编码>
 *
 *   res     = products/<产品ID>/devices/<设备名称>
 *   et      = token过期的Unix时间戳（本工程无RTC，固定写死为2030-01-01）
 *   method  = sha1
 *   version = 2018-10-31
 */

//根据 onenet.h 中的三元组生成完整token字符串，写入out
void OneNET_BuildToken(u8 *out,u16 out_len);

#endif
