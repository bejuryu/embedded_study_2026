import os

svgs = {
    "backspace.svg": """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#FFFFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M20 4H8.5l-6.5 8 6.5 8H20c1.097 0 2-0.903 2-2V6c0-1.097-0.903-2-2-2z"/>
  <line x1="17" y1="9" x2="11" y2="15"/>
  <line x1="11" y1="9" x2="17" y2="15"/>
</svg>""",

    "battery.svg": """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#FFFFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M 4 7 h 11 c 1.104 0, 2 0.896, 2 2 v 6 c 0 1.104, -0.896 2, -2 2 h -11 c -1.104 0, -2 -0.896, -2 -2 v -6 c 0 -1.104, 0.896 -2, 2 -2 z"/>
  <line x1="21" y1="11" x2="21" y2="13"/>
</svg>""",

    "ble.svg": """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#FFFFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M6.5 6.5l11 11L12 22V2l5.5 5.5L6.5 17.5"/>
</svg>""",

    "enter.svg": """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#FFFFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M9 10L4 15L9 20"/>
  <path d="M19 5v6c0 2.209-1.791 4-4 4H4"/>
</svg>""",

    "heart.svg": """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#FFFFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M20.84 4.61C19.809 3.578 18.409 2.998 16.95 2.998 C15.491 2.998 14.091 3.578 13.06 4.610L12 5.67l-1.06-1.06C8.806 2.476 5.294 2.476 3.16 4.610 C1.026 6.744 1.026 10.256 3.16 12.390l1.06 1.06L12 21.23l7.78-7.78 1.06-1.06C21.872 11.359 22.452 9.959 22.452 8.500 C22.452 7.041 21.872 5.641 20.84 4.610z"/>
</svg>""",

    "mute.svg": """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#FFFFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5"/>
  <line x1="22" y1="9" x2="16" y2="15"/>
  <line x1="16" y1="9" x2="22" y2="15"/>
</svg>""",

    "next.svg": """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#FFFFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <polygon points="5 4 15 12 5 20 5 4"/>
  <line x1="19" y1="5" x2="19" y2="19"/>
</svg>""",

    "pause.svg": """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#FFFFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <rect x="6" y="4" width="4" height="16"/>
  <rect x="14" y="4" width="4" height="16"/>
</svg>""",

    "play.svg": """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#FFFFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <polygon points="6 3 18 12 6 21 6 3"/>
</svg>""",

    "play_pause.svg": """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#FFFFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <polygon points="3 5 11 12 3 19 3 5" fill="#FFFFFF"/>
  <rect x="15" y="5" width="2.5" height="14" fill="#FFFFFF"/>
  <rect x="20" y="5" width="2.5" height="14" fill="#FFFFFF"/>
</svg>""",

    "prev.svg": """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#FFFFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <polygon points="19 20 9 12 19 4 19 20"/>
  <line x1="5" y1="19" x2="5" y2="5"/>
</svg>""",

    "repeat.svg": """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#FFFFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M17 2L21 5L17 8"/>
  <path d="M3 11V9c0-2.209 1.791-4 4-4h14"/>
  <path d="M7 22L3 19L7 16"/>
  <path d="M21 13v2c0 2.209-1.791 4-4 4H3"/>
</svg>""",

    "shuffle.svg": """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#FFFFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M16 4L20 4L20 8"/>
  <path d="M4 20L20 4"/>
  <path d="M20 16L20 20L16 20"/>
  <path d="M15 15L20 20"/>
  <path d="M4 4L9 9"/>
</svg>""",

    "vol_down.svg": """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#FFFFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5"/>
  <path d="M15.54 8.46C17.479 10.399 17.479 13.591 15.540 15.530"/>
</svg>""",

    "vol_up.svg": """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#FFFFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5"/>
  <path d="M15.54 8.46C17.479 10.399 17.479 13.591 15.540 15.530"/>
  <path d="M19.07 4.93C22.948 8.809 22.948 15.191 19.070 19.070"/>
</svg>""",

    "wifi.svg": """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#FFFFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M5.0 12.55C9.062 9.166 15.018 9.166 19.080 12.550"/>
  <path d="M1.42 9.0C7.438 3.695 16.562 3.695 22.580 9.000"/>
  <path d="M8.53 16.1C10.606 14.625 13.404 14.625 15.480 16.100"/>
  <line x1="12" y1="20" x2="12.01" y2="20" stroke-width="3"/>
</svg>"""
}

icons_dir = "d:/workspace/embedded_m5stack_tab5/src/009.touchpad/main/display/icons"
for filename, content in svgs.items():
    filepath = os.path.join(icons_dir, filename)
    with open(filepath, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"Updated {filename}")
