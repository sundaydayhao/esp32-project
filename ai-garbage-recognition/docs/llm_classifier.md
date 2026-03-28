# LLM Image Classifier Contract

This firmware now supports:

`ESP32-S3 camera -> JPEG upload -> your backend -> LLM vision model -> normalized 4-class response`

## Firmware Request

The device sends:

- Method: `POST`
- URL: `ONLINE_API_URL?locale=zh-CN&standard=CN-4&prompt_version=garbage-cn-v1`
- Header: `Content-Type: image/jpeg`
- Header: `Authorization: Bearer <token>` when `ONLINE_API_AUTH_TOKEN` is configured
- Body: raw JPEG bytes from the current camera frame

## Expected Response

Return JSON like:

```json
{
  "type": "recyclable",
  "confidence": 94,
  "reason": "The image shows paper bags, which are recyclable."
}
```

Allowed `type` values:

- `hazardous`
- `recyclable`
- `kitchen`
- `other`

The firmware also accepts `category` or `name_cn` if you want to return a different key, but `type` is preferred.

## Backend Prompt

Recommended system prompt:

```text
You classify household waste under the China four-bin standard.
You receive one image.
Return JSON only.
Allowed type values: hazardous, recyclable, kitchen, other.
If uncertain, choose the most likely category and lower confidence.
```

## Notes

- Keep provider keys on the backend, not in firmware.
- If you need city-specific rules, inject city context on the backend.
- The firmware currently defaults to `MODE_TFLITE_ONLINE`, which means the camera capture path now expects WiFi and the online image API.
