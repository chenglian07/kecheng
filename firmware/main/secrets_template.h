#ifndef SECRETS_TEMPLATE_H
#define SECRETS_TEMPLATE_H

/*
 * 使用前请复制本文件为 secrets.h，并填入实际的值。
 * secrets.h 已被 .gitignore 排除，不会提交到 Git。
 *
 * 用法：
 *   cp main/secrets_template.h main/secrets.h
 *   然后编辑 main/secrets.h 填入实际参数。
 */

// WiFi 凭据（学校网络）
#define WIFI_SSID           "your_wifi_ssid"
#define WIFI_PASS           "your_wifi_password"

// 设备标识（第 1 周仅用于串口 JSON 输出）
#define DEVICE_ID           "g02-s20251040108-esp32s3eye"

// 服务器地址（第 2 周开始使用）
#define SERVER_BASE_URL     "http://10.1.41.154:8000"

#endif // SECRETS_TEMPLATE_H