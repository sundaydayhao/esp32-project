"""
Generate SVG diagrams for ESP32-S3 Oscilloscope project.
Uses Python's built-in capabilities (no external dependencies).
Run: python docs/diagrams/generate_svg.py
"""

import os

OUTPUT_DIR = os.path.dirname(os.path.abspath(__file__))


def write_svg(filename, content):
    path = os.path.join(OUTPUT_DIR, filename)
    with open(path, 'w', encoding='utf-8') as f:
        f.write(content)
    print(f"Written: {path}")


# ─────────────────────────────────────────────────────────────────────────────
# Module Dependency SVG
# ─────────────────────────────────────────────────────────────────────────────
MODULE_DEP_SVG = '''<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="900" height="620" viewBox="0 0 900 620">
  <defs>
    <marker id="arrow" markerWidth="10" markerHeight="7" refX="9" refY="3.5" orient="auto">
      <polygon points="0 0, 10 3.5, 0 7" fill="#555"/>
    </marker>
    <marker id="arrow-blue" markerWidth="10" markerHeight="7" refX="9" refY="3.5" orient="auto">
      <polygon points="0 0, 10 3.5, 0 7" fill="#3498db"/>
    </marker>
    <marker id="arrow-orange" markerWidth="10" markerHeight="7" refX="9" refY="3.5" orient="auto">
      <polygon points="0 0, 10 3.5, 0 7" fill="#e67e22"/>
    </marker>
    <style>
      text { font-family: Consolas, monospace; }
      .title { font-size: 16px; font-weight: bold; fill: #2c3e50; }
      .node-label { font-size: 11px; fill: #2c3e50; }
      .node-sub { font-size: 9px; fill: #7f8c8d; }
      .edge-label { font-size: 8px; fill: #7f8c8d; }
      .legend-text { font-size: 10px; fill: #555; }
    </style>
  </defs>

  <!-- Background -->
  <rect width="900" height="620" fill="#f8f9fa" rx="8"/>

  <!-- Title -->
  <text x="450" y="28" text-anchor="middle" class="title">ESP32-S3 Oscilloscope — Module Dependency Graph</text>
  <text x="450" y="44" text-anchor="middle" style="font-size:10px;fill:#7f8c8d">Solid = #include / compile-time | Dashed = runtime data flow</text>

  <!-- ── Row 1: main.c ── -->
  <rect x="350" y="60" width="200" height="50" rx="6" fill="#e74c3c" stroke="#c0392b" stroke-width="2"/>
  <text x="450" y="80" text-anchor="middle" class="node-label" fill="white" font-weight="bold">main/main.c</text>
  <text x="450" y="96" text-anchor="middle" class="node-sub" fill="#fde8e8">app_main() — 入口</text>

  <!-- ── Row 2: wifi_ap.c, adc_sampler.c, http_server.c ── -->
  <rect x="60" y="160" width="180" height="50" rx="6" fill="#aed6f1" stroke="#5dade2" stroke-width="1.5"/>
  <text x="150" y="180" text-anchor="middle" class="node-label">main/wifi_ap.c</text>
  <text x="150" y="196" text-anchor="middle" class="node-sub">WiFi AP 管理</text>

  <rect x="360" y="160" width="180" height="50" rx="6" fill="#a9dfbf" stroke="#52be80" stroke-width="1.5"/>
  <text x="450" y="180" text-anchor="middle" class="node-label">main/adc_sampler.c</text>
  <text x="450" y="196" text-anchor="middle" class="node-sub">ADC 采集 + 双缓冲</text>

  <rect x="660" y="160" width="180" height="50" rx="6" fill="#f9e79f" stroke="#f1c40f" stroke-width="1.5"/>
  <text x="750" y="180" text-anchor="middle" class="node-label">main/http_server.c</text>
  <text x="750" y="196" text-anchor="middle" class="node-sub">HTTP + WebSocket</text>

  <!-- ── Row 3: Web files ── -->
  <rect x="560" y="280" width="140" height="45" rx="6" fill="#fadbd8" stroke="#e74c3c" stroke-width="1"/>
  <text x="630" y="298" text-anchor="middle" class="node-label">web/index.html</text>
  <text x="630" y="313" text-anchor="middle" class="node-sub">UI 结构</text>

  <rect x="720" y="280" width="140" height="45" rx="6" fill="#f1948a" stroke="#e74c3c" stroke-width="1"/>
  <text x="790" y="298" text-anchor="middle" class="node-label">web/app.js</text>
  <text x="790" y="313" text-anchor="middle" class="node-sub">波形引擎 (647行)</text>

  <rect x="400" y="280" width="140" height="45" rx="6" fill="#f5cba7" stroke="#e67e22" stroke-width="1"/>
  <text x="470" y="298" text-anchor="middle" class="node-label">web/style.css</text>
  <text x="470" y="313" text-anchor="middle" class="node-sub">深色主题 (498行)</text>

  <!-- ── Row 4: ESP-IDF components ── -->
  <rect x="30" y="390" width="110" height="40" rx="20" fill="#d5d8dc" stroke="#aab7b8" stroke-width="1"/>
  <text x="85" y="406" text-anchor="middle" class="node-label">nvs_flash</text>
  <text x="85" y="420" text-anchor="middle" class="node-sub">NVS</text>

  <rect x="155" y="390" width="110" height="40" rx="20" fill="#d5d8dc" stroke="#aab7b8" stroke-width="1"/>
  <text x="210" y="406" text-anchor="middle" class="node-label">esp_wifi</text>
  <text x="210" y="420" text-anchor="middle" class="node-sub">WiFi 驱动</text>

  <rect x="280" y="390" width="110" height="40" rx="20" fill="#d5d8dc" stroke="#aab7b8" stroke-width="1"/>
  <text x="335" y="406" text-anchor="middle" class="node-label">esp_adc</text>
  <text x="335" y="420" text-anchor="middle" class="node-sub">ADC 驱动</text>

  <rect x="405" y="390" width="110" height="40" rx="20" fill="#d5d8dc" stroke="#aab7b8" stroke-width="1"/>
  <text x="460" y="406" text-anchor="middle" class="node-label">FreeRTOS</text>
  <text x="460" y="420" text-anchor="middle" class="node-sub">RTOS</text>

  <rect x="530" y="390" width="110" height="40" rx="20" fill="#d5d8dc" stroke="#aab7b8" stroke-width="1"/>
  <text x="585" y="406" text-anchor="middle" class="node-label">esp_http_server</text>
  <text x="585" y="420" text-anchor="middle" class="node-sub">HTTP/WS</text>

  <rect x="655" y="390" width="110" height="40" rx="20" fill="#d5d8dc" stroke="#aab7b8" stroke-width="1"/>
  <text x="710" y="406" text-anchor="middle" class="node-label">cJSON</text>
  <text x="710" y="420" text-anchor="middle" class="node-sub">JSON 库</text>

  <rect x="780" y="390" width="100" height="40" rx="20" fill="#d5d8dc" stroke="#aab7b8" stroke-width="1"/>
  <text x="830" y="406" text-anchor="middle" class="node-label">LwIP</text>
  <text x="830" y="420" text-anchor="middle" class="node-sub">TCP/IP</text>

  <!-- ── Edges: main.c → modules ── -->
  <line x1="400" y1="110" x2="200" y2="160" stroke="#555" stroke-width="1.5" marker-end="url(#arrow)"/>
  <line x1="450" y1="110" x2="450" y2="160" stroke="#555" stroke-width="1.5" marker-end="url(#arrow)"/>
  <line x1="500" y1="110" x2="700" y2="160" stroke="#555" stroke-width="1.5" marker-end="url(#arrow)"/>

  <!-- ── Edges: http_server.c → web files (dashed orange) ── -->
  <line x1="750" y1="210" x2="750" y2="280" stroke="#e67e22" stroke-width="1.5" stroke-dasharray="5,3" marker-end="url(#arrow-orange)"/>
  <line x1="730" y1="210" x2="650" y2="280" stroke="#e67e22" stroke-width="1.5" stroke-dasharray="5,3" marker-end="url(#arrow-orange)"/>
  <line x1="710" y1="210" x2="510" y2="280" stroke="#e67e22" stroke-width="1.5" stroke-dasharray="5,3" marker-end="url(#arrow-orange)"/>
  <text x="800" y="248" class="edge-label">EMBED_FILES</text>

  <!-- ── Edge: adc_sampler → http_server (runtime, dashed blue) ── -->
  <line x1="540" y1="185" x2="660" y2="185" stroke="#3498db" stroke-width="2" stroke-dasharray="6,3" marker-end="url(#arrow-blue)"/>
  <text x="590" y="178" class="edge-label" fill="#3498db">get_frame()</text>

  <!-- ── Edges: modules → ESP-IDF ── -->
  <line x1="120" y1="210" x2="85" y2="390" stroke="#aab7b8" stroke-width="1" marker-end="url(#arrow)"/>
  <line x1="150" y1="210" x2="210" y2="390" stroke="#aab7b8" stroke-width="1" marker-end="url(#arrow)"/>
  <line x1="420" y1="210" x2="335" y2="390" stroke="#aab7b8" stroke-width="1" marker-end="url(#arrow)"/>
  <line x1="450" y1="210" x2="460" y2="390" stroke="#aab7b8" stroke-width="1" marker-end="url(#arrow)"/>
  <line x1="720" y1="210" x2="585" y2="390" stroke="#aab7b8" stroke-width="1" marker-end="url(#arrow)"/>
  <line x1="760" y1="210" x2="710" y2="390" stroke="#aab7b8" stroke-width="1" marker-end="url(#arrow)"/>
  <line x1="800" y1="210" x2="830" y2="390" stroke="#aab7b8" stroke-width="1" marker-end="url(#arrow)"/>

  <!-- ── Legend ── -->
  <rect x="30" y="470" width="840" height="120" rx="6" fill="white" stroke="#ddd" stroke-width="1"/>
  <text x="50" y="490" class="legend-text" font-weight="bold">图例 / Legend</text>
  <rect x="50" y="500" width="20" height="12" fill="#e74c3c" rx="2"/>
  <text x="76" y="511" class="legend-text">入口 (Entry Point)</text>
  <rect x="200" y="500" width="20" height="12" fill="#aed6f1" rx="2"/>
  <text x="226" y="511" class="legend-text">固件模块 (Firmware Module)</text>
  <rect x="420" y="500" width="20" height="12" fill="#fadbd8" rx="2"/>
  <text x="446" y="511" class="legend-text">Web 文件 (Embedded Web)</text>
  <rect x="620" y="500" width="20" height="12" fill="#d5d8dc" rx="20"/>
  <text x="646" y="511" class="legend-text">ESP-IDF 组件</text>
  <line x1="50" y1="535" x2="80" y2="535" stroke="#555" stroke-width="1.5" marker-end="url(#arrow)"/>
  <text x="86" y="539" class="legend-text">#include / 编译依赖</text>
  <line x1="250" y1="535" x2="280" y2="535" stroke="#e67e22" stroke-width="1.5" stroke-dasharray="5,3" marker-end="url(#arrow-orange)"/>
  <text x="286" y="539" class="legend-text">EMBED_FILES (编译时嵌入)</text>
  <line x1="500" y1="535" x2="530" y2="535" stroke="#3498db" stroke-width="2" stroke-dasharray="6,3" marker-end="url(#arrow-blue)"/>
  <text x="536" y="539" class="legend-text">运行时数据流 (Runtime Data)</text>
  <text x="50" y="565" class="legend-text" fill="#7f8c8d">注：DOT 源文件见 docs/diagrams/module_dependency.dot，可用 Graphviz dot 命令渲染更详细版本</text>
  <text x="50" y="580" class="legend-text" fill="#7f8c8d">    命令：dot -Tsvg docs/diagrams/module_dependency.dot -o docs/diagrams/module_dependency_full.svg</text>
</svg>'''

# ─────────────────────────────────────────────────────────────────────────────
# Call Graph SVG (simplified)
# ─────────────────────────────────────────────────────────────────────────────
CALLGRAPH_SVG = '''<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="1000" height="700" viewBox="0 0 1000 700">
  <defs>
    <marker id="arr" markerWidth="10" markerHeight="7" refX="9" refY="3.5" orient="auto">
      <polygon points="0 0, 10 3.5, 0 7" fill="#555"/>
    </marker>
    <marker id="arr-task" markerWidth="10" markerHeight="7" refX="9" refY="3.5" orient="auto">
      <polygon points="0 0, 10 3.5, 0 7" fill="#e67e22"/>
    </marker>
    <style>
      text { font-family: Consolas, monospace; }
      .title { font-size: 15px; font-weight: bold; fill: #2c3e50; }
      .fn { font-size: 10px; fill: #2c3e50; }
      .loc { font-size: 8px; fill: #7f8c8d; }
      .el { font-size: 8px; fill: #555; }
    </style>
  </defs>
  <rect width="1000" height="700" fill="#f8f9fa" rx="8"/>
  <text x="500" y="26" text-anchor="middle" class="title">ESP32-S3 Oscilloscope — Firmware Call Graph (Static Analysis)</text>

  <!-- ── app_main ── -->
  <rect x="420" y="45" width="160" height="40" rx="5" fill="#e74c3c" stroke="#c0392b" stroke-width="2"/>
  <text x="500" y="62" text-anchor="middle" class="fn" fill="white" font-weight="bold">app_main()</text>
  <text x="500" y="76" text-anchor="middle" class="loc" fill="#fde8e8">main.c:16</text>

  <!-- ── Level 2: init functions ── -->
  <rect x="30" y="140" width="160" height="40" rx="5" fill="#5dade2" stroke="#2e86c1" stroke-width="1.5"/>
  <text x="110" y="157" text-anchor="middle" class="fn" fill="white">adc_sampler_init()</text>
  <text x="110" y="171" text-anchor="middle" class="loc" fill="#d6eaf8">adc_sampler.c:140</text>

  <rect x="220" y="140" width="160" height="40" rx="5" fill="#5dade2" stroke="#2e86c1" stroke-width="1.5"/>
  <text x="300" y="157" text-anchor="middle" class="fn" fill="white">wifi_ap_init()</text>
  <text x="300" y="171" text-anchor="middle" class="loc" fill="#d6eaf8">wifi_ap.c:35</text>

  <rect x="620" y="140" width="160" height="40" rx="5" fill="#5dade2" stroke="#2e86c1" stroke-width="1.5"/>
  <text x="700" y="157" text-anchor="middle" class="fn" fill="white">http_server_start()</text>
  <text x="700" y="171" text-anchor="middle" class="loc" fill="#d6eaf8">http_server.c:261</text>

  <!-- ── Tasks (orange) ── -->
  <rect x="30" y="260" width="160" height="50" rx="5" fill="#e67e22" stroke="#ca6f1e" stroke-width="1.5"/>
  <text x="110" y="278" text-anchor="middle" class="fn" fill="white">adc_sampler_task()</text>
  <text x="110" y="292" text-anchor="middle" class="loc" fill="#fdebd0">adc_sampler.c:83</text>
  <text x="110" y="304" text-anchor="middle" class="loc" fill="#fdebd0">[Core1 pri=5]</text>

  <rect x="620" y="260" width="160" height="50" rx="5" fill="#e67e22" stroke="#ca6f1e" stroke-width="1.5"/>
  <text x="700" y="278" text-anchor="middle" class="fn" fill="white">ws_broadcast_task()</text>
  <text x="700" y="292" text-anchor="middle" class="loc" fill="#fdebd0">http_server.c:76</text>
  <text x="700" y="304" text-anchor="middle" class="loc" fill="#fdebd0">[Core0 pri=4]</text>

  <!-- ── HTTP handlers ── -->
  <rect x="430" y="260" width="160" height="40" rx="5" fill="#f9e79f" stroke="#f1c40f" stroke-width="1.5"/>
  <text x="510" y="277" text-anchor="middle" class="fn">HTTP handlers</text>
  <text x="510" y="291" text-anchor="middle" class="loc">index/js/css/info/config</text>

  <rect x="430" y="320" width="160" height="40" rx="5" fill="#f9e79f" stroke="#f1c40f" stroke-width="1.5"/>
  <text x="510" y="337" text-anchor="middle" class="fn">ws_handler()</text>
  <text x="510" y="351" text-anchor="middle" class="loc">http_server.c:183</text>

  <!-- ── adc_sampler public API ── -->
  <rect x="220" y="260" width="160" height="40" rx="5" fill="#a9dfbf" stroke="#52be80" stroke-width="1.5"/>
  <text x="300" y="277" text-anchor="middle" class="fn">adc_sampler_get_frame()</text>
  <text x="300" y="291" text-anchor="middle" class="loc">adc_sampler.c:193</text>

  <rect x="220" y="320" width="160" height="40" rx="5" fill="#a9dfbf" stroke="#52be80" stroke-width="1.5"/>
  <text x="300" y="337" text-anchor="middle" class="fn">adc_sampler_set_rate()</text>
  <text x="300" y="351" text-anchor="middle" class="loc">adc_sampler.c:207</text>

  <!-- ── ESP-IDF / FreeRTOS ── -->
  <rect x="30" y="420" width="130" height="35" rx="15" fill="#d5d8dc" stroke="#aab7b8"/>
  <text x="95" y="435" text-anchor="middle" class="fn">adc_oneshot_read()</text>
  <text x="95" y="448" text-anchor="middle" class="loc">[esp_adc]</text>

  <rect x="175" y="420" width="130" height="35" rx="15" fill="#d5d8dc" stroke="#aab7b8"/>
  <text x="240" y="435" text-anchor="middle" class="fn">xSemaphoreTake/Give()</text>
  <text x="240" y="448" text-anchor="middle" class="loc">[FreeRTOS]</text>

  <rect x="320" y="420" width="130" height="35" rx="15" fill="#d5d8dc" stroke="#aab7b8"/>
  <text x="385" y="435" text-anchor="middle" class="fn">httpd_ws_send_async()</text>
  <text x="385" y="448" text-anchor="middle" class="loc">[esp_http_server]</text>

  <rect x="465" y="420" width="130" height="35" rx="15" fill="#d5d8dc" stroke="#aab7b8"/>
  <text x="530" y="435" text-anchor="middle" class="fn">cJSON_*()</text>
  <text x="530" y="448" text-anchor="middle" class="loc">[cJSON]</text>

  <rect x="610" y="420" width="130" height="35" rx="15" fill="#d5d8dc" stroke="#aab7b8"/>
  <text x="675" y="435" text-anchor="middle" class="fn">esp_wifi_*()</text>
  <text x="675" y="448" text-anchor="middle" class="loc">[esp_wifi]</text>

  <rect x="755" y="420" width="130" height="35" rx="15" fill="#d5d8dc" stroke="#aab7b8"/>
  <text x="820" y="435" text-anchor="middle" class="fn">xTaskCreatePinnedToCore()</text>
  <text x="820" y="448" text-anchor="middle" class="loc">[FreeRTOS]</text>

  <!-- ── Edges: app_main → init ── -->
  <line x1="460" y1="85" x2="160" y2="140" stroke="#555" stroke-width="1.5" marker-end="url(#arr)"/>
  <line x1="480" y1="85" x2="340" y2="140" stroke="#555" stroke-width="1.5" marker-end="url(#arr)"/>
  <line x1="540" y1="85" x2="660" y2="140" stroke="#555" stroke-width="1.5" marker-end="url(#arr)"/>

  <!-- ── Edges: adc_sampler_init → task ── -->
  <line x1="110" y1="180" x2="110" y2="260" stroke="#e67e22" stroke-width="1.5" stroke-dasharray="5,3" marker-end="url(#arr-task)"/>
  <text x="115" y="225" class="el" fill="#e67e22">spawns</text>

  <!-- ── Edges: http_server_start → task ── -->
  <line x1="700" y1="180" x2="700" y2="260" stroke="#e67e22" stroke-width="1.5" stroke-dasharray="5,3" marker-end="url(#arr-task)"/>
  <text x="705" y="225" class="el" fill="#e67e22">spawns</text>

  <!-- ── Edges: http_server_start → handlers ── -->
  <line x1="660" y1="180" x2="560" y2="260" stroke="#555" stroke-width="1" marker-end="url(#arr)"/>
  <line x1="650" y1="185" x2="560" y2="320" stroke="#555" stroke-width="1" marker-end="url(#arr)"/>

  <!-- ── Edges: ws_broadcast_task → adc_sampler_get_frame ── -->
  <line x1="620" y1="280" x2="380" y2="280" stroke="#3498db" stroke-width="2" marker-end="url(#arr)"/>
  <text x="490" y="273" class="el" fill="#3498db">get_frame()</text>

  <!-- ── Edges: ws_handler → adc_sampler_set_rate ── -->
  <line x1="510" y1="360" x2="380" y2="340" stroke="#555" stroke-width="1" marker-end="url(#arr)"/>

  <!-- ── Edges: tasks → ESP-IDF ── -->
  <line x1="95" y1="310" x2="95" y2="420" stroke="#aab7b8" stroke-width="1" marker-end="url(#arr)"/>
  <line x1="110" y1="310" x2="240" y2="420" stroke="#aab7b8" stroke-width="1" marker-end="url(#arr)"/>
  <line x1="700" y1="310" x2="385" y2="420" stroke="#aab7b8" stroke-width="1" marker-end="url(#arr)"/>
  <line x1="510" y1="300" x2="530" y2="420" stroke="#aab7b8" stroke-width="1" marker-end="url(#arr)"/>
  <line x1="300" y1="180" x2="675" y2="420" stroke="#aab7b8" stroke-width="1" marker-end="url(#arr)"/>
  <line x1="820" y1="180" x2="820" y2="420" stroke="#aab7b8" stroke-width="1" marker-end="url(#arr)"/>

  <!-- ── Legend ── -->
  <rect x="30" y="490" width="940" height="180" rx="6" fill="white" stroke="#ddd" stroke-width="1"/>
  <text x="50" y="510" class="fn" font-weight="bold">图例 / Legend</text>
  <rect x="50" y="520" width="18" height="12" fill="#e74c3c" rx="2"/>
  <text x="74" y="531" class="el">入口函数</text>
  <rect x="160" y="520" width="18" height="12" fill="#5dade2" rx="2"/>
  <text x="184" y="531" class="el">公共 API</text>
  <rect x="270" y="520" width="18" height="12" fill="#e67e22" rx="2"/>
  <text x="294" y="531" class="el">FreeRTOS 任务</text>
  <rect x="400" y="520" width="18" height="12" fill="#a9dfbf" rx="2"/>
  <text x="424" y="531" class="el">ADC 模块 API</text>
  <rect x="530" y="520" width="18" height="12" fill="#f9e79f" rx="2"/>
  <text x="554" y="531" class="el">HTTP 回调</text>
  <rect x="640" y="520" width="18" height="12" fill="#d5d8dc" rx="15"/>
  <text x="664" y="531" class="el">ESP-IDF / FreeRTOS API</text>
  <line x1="50" y1="555" x2="80" y2="555" stroke="#555" stroke-width="1.5" marker-end="url(#arr)"/>
  <text x="86" y="559" class="el">直接调用</text>
  <line x1="180" y1="555" x2="210" y2="555" stroke="#e67e22" stroke-width="1.5" stroke-dasharray="5,3" marker-end="url(#arr-task)"/>
  <text x="216" y="559" class="el">创建任务</text>
  <line x1="310" y1="555" x2="340" y2="555" stroke="#3498db" stroke-width="2" marker-end="url(#arr)"/>
  <text x="346" y="559" class="el">跨模块数据读取</text>
  <text x="50" y="590" class="el" fill="#7f8c8d">注意事项：</text>
  <text x="50" y="605" class="el" fill="#7f8c8d">1. 此图为静态分析生成，函数指针/回调（wifi_event_handler, HTTP handlers）以虚线标注</text>
  <text x="50" y="620" class="el" fill="#7f8c8d">2. ESP-IDF 内部调用链未展开（如 httpd 内部任务）</text>
  <text x="50" y="635" class="el" fill="#7f8c8d">3. DOT 源文件：docs/diagrams/callgraph_firmware.dot（需 Graphviz dot 命令渲染完整版）</text>
  <text x="50" y="650" class="el" fill="#7f8c8d">   命令：dot -Tsvg docs/diagrams/callgraph_firmware.dot -o docs/diagrams/callgraph_firmware_full.svg</text>
  <text x="50" y="665" class="el" fill="#7f8c8d">4. 行号基于静态分析，实际行号可能因编辑器格式化略有偏差</text>
</svg>'''


if __name__ == '__main__':
    write_svg('module_dependency.svg', MODULE_DEP_SVG)
    write_svg('callgraph_firmware.svg', CALLGRAPH_SVG)
    print('Done. SVG files generated in docs/diagrams/')